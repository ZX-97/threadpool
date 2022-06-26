#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;//单位秒

//线程池支持的模式
enum class PoolMode
{
	MODE_FIXED,  //固定数量线程
	MODE_CACHED, //线程数量可动态增长
};

//线程类型
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;
	//线程构造函数
	Thread(ThreadFunc func)
		:func_(func)
		,threadId_(generatedId_++)
	{}
	//线程析构函数
	~Thread() = default;
	//启动线程
	void start()
	{
		//创建一个线程来执行线程函数
	//ThreadPool::threadFunc() -> Thread类中绑定接收 -> func -> func_
		std::thread t(func_, threadId_);//c++11来将，线程对象t   和  线程执行函数func_
		t.detach(); //设置分离线程    线程对象和线程执行函数分开，，，线程对象t离开作用域会被析构，detach之后不影响线程函数的执行
	}
	//获取线程id
	int getId() const
	{
		return threadId_;
	}
private:

	ThreadFunc func_;
	int threadId_;//保存线程id
	static int generatedId_;
};

int Thread::generatedId_ = 0;

//线程池类型
class ThreadPool
{
public:
	//线程池构造
	ThreadPool()
		:initThreadSize_(0)
		, taskSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
		, idleThreadSize_(0)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, curThreadSize_(0)
	{}

	//线程池析构
	~ThreadPool()
	{
		isPoolRunning_ = false;

		//notEmpty_.notify_all();//唤醒因为没有Task执行，阻塞在notEmpty_.wait_for上的线程，阻塞 -》等待-》抢锁
		//等待线程池里的线程返回   有两种状态： 阻塞 & 正在执行任务中

		std::unique_lock<std::mutex> lock(taskQueMtx_);

		notEmpty_.notify_all();

		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	//设置线程的工作模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
		{
			return;
		}
		poolMode_ = mode;
	}

	//设置设置线程上限阈值
	void setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState())
		{
			return;
		}
		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThreshHold_ = threshhold;
		}
	}

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
		{
			return;
		}
		taskQueMaxThreshHold_ = threshhold;
	}

	//给线程池提交任务
	//使用可变参模板编程，让submitTask 可以接收任意任务函数和任意数量的参数
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//打包任务 放到任务队列中
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//线程通信，等待任务队列有空余  notFull_
		//用户提交任务，最长不能阻塞超过1s,否则判断任务提交失败，返回
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			//表示notFull_等待1s，条件依然没有满足
			std::cerr << "task queue is full, submit task faill." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		//如果有空余，把任务放到任务队列中
		taskQue_.emplace([task](){ (*task)(); });
		taskSize_++;

		//因为放了新任务，任务队列肯定不空了，在notEmpty_上进行通知
		notEmpty_.notify_all();

		//需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << ">>>>>create new thread...." << std::endl;
			//创建新线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//启动线程

			//修改线程个数相关变量
			curThreadSize_++; //当前线程数量
			idleThreadSize_++;//空闲线程
		}

		//返回任务的Result对象
		//return task->getResult();
		return result;


	}

	//开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//设置线程池运行状态
		isPoolRunning_ = true;

		//记录初始线程个数
		initThreadSize_ = initThreadSize;
		//记录当前线程个数
		curThreadSize_ = initThreadSize;

		//集中创建线程对象
		for (int i = 0; i < initThreadSize_; i++)
		{
			//创建thread线程对象的时候，把线程函数给到thread线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}

		//启动所有线程
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++;//记录空闲线程数量
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//定义线程启动函数
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		for (;;)
		{
			Task task;
			{
				//先获取锁
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
				//cached模式下，有可能已经创建了很多的线程。
				//但是空闲时间超过60s，应该把多余的线程结束回收掉
				//超过initThreadSize_数量的线程要进行回收
				//当前时间 - 上一次线程执行时间 > 60s
				//锁 + 双重判断 避免死锁
				while (taskQue_.size() == 0)
				{
					//线程池要结束，回收线程资源
					if (!isPoolRunning_) {
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_.notify_all();
						return;
					}
					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						if (std::cv_status::timeout ==
							notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_)
							{
								//开始回收当前线程
								//1.记录线程数量的相关变量进行变化

								//2.把线程对象从线程列表容器中删除
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
								return;
							}
						}
					}
					else
					{
						//等待notEmpty条件
						notEmpty_.wait(lock);
					}
				}

				idleThreadSize_--; //空闲线程减1
				std::cout << "tid:" << std::this_thread::get_id() << "任务获取成功..." << std::endl;
				//从任务队列中取一个任务出来
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//如果有剩余任务，继续通知其他线程执行任务
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				//取出任务后进行通知,继续生产任务
				notFull_.notify_all();

			}//操作完任务队列 把锁释放掉

			//当前线程负责执行这个任务
			if (task != nullptr)
			{
				//task->run();//执行任务；把执行任务的返回值通过setVal方法给到Result
				task(); //using Task = std::function<void()> 里面包含着packaged_task打包好的任务
			}
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
		}
	}
	//检查pool的运行状态
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}
private:
	//std::vector<std::unique_ptr<Thread>> threads_; //线程列表
	std::unordered_map<int, std::unique_ptr<Thread>>threads_; //线程列表
	int initThreadSize_; //初始线程数量
	std::atomic_int idleThreadSize_; //空闲线程数量
	std::atomic_int curThreadSize_;//记录当前线程总数量
	int threadSizeThreshHold_; //线程数量上限阈值

	using Task = std::function<void()>;
	std::queue<Task> taskQue_; //任务队列
	std::atomic_int taskSize_; //任务数量
	int taskQueMaxThreshHold_; //任务队列上限阈值

	std::mutex taskQueMtx_;//保证任务队列线程安全
	std::condition_variable notFull_; //表示任务队列不满
	std::condition_variable notEmpty_;//表示任务队列不空
	std::condition_variable exitCond_;//等待线程资源全部回收

	PoolMode poolMode_; // 当前线程池的工作模式
	std::atomic_bool isPoolRunning_; //表示线程池的工作状态

};


#endif

