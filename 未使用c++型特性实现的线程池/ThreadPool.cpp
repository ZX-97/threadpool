#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <chrono>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;//单位秒

///////////////////线程池方法实现

//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
	,isPoolRunning_(false)
	,idleThreadSize_(0)
	,threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, curThreadSize_(0)
{}

//线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	//notEmpty_.notify_all();//唤醒因为没有Task执行，阻塞在notEmpty_.wait_for上的线程，阻塞 -》等待-》抢锁
	//等待线程池里的线程返回   有两种状态： 阻塞 & 正在执行任务中

	std::unique_lock<std::mutex> lock(taskQueMtx_);

	notEmpty_.notify_all();

	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

//设置线程的工作模式
void ThreadPool::setMode(PoolMode mode) 
{
	if (checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

//设置设置线程上限阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
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
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}

//给线程池提交任务 用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程通信，等待任务队列有空余  notFull_
	//用户提交任务，最长不能阻塞超过1s,否则判断任务提交失败，返回
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_;}))
	{
		//表示notFull_等待1s，条件依然没有满足
		std::cerr << "task queue is full, submit task faill." << std::endl;
		//return task->getResult(); //result 依赖于task 线程执行完task task对象就析构掉了
		return Result(sp, false);
	}

	//如果有空余，把任务放到任务队列中
	taskQue_.emplace(sp);
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
	return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize)
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
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
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

//定义线程启动函数   线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	for(;;)
	{
		std::shared_ptr<Task> task;
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
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}
}

//检查线程池是否运行中
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}


////////////////////////线程方法实现

int Thread::generatedId_ = 0;

//启动线程
void Thread::start()
{
	//创建一个线程来执行线程函数
	//ThreadPool::threadFunc() -> Thread类中绑定接收 -> func -> func_
	std::thread t(func_, threadId_);//c++11来将，线程对象t   和  线程执行函数func_
	t.detach(); //设置分离线程    线程对象和线程执行函数分开，，，线程对象t离开作用域会被析构，detach之后不影响线程函数的执行
}

//线程构造函数
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generatedId_++)
{}

//线程析构函数
Thread::~Thread()
{}

//获取线程id
int Thread::getId() const
{
	return threadId_;
}

////////////////////Task方法实现
Task::Task()
	:result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());  //这里发生多态调用
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}

////////////////////获取返回值方法实现 Result
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task_->setResult(this);
}

Any Result::get()//用户来调用
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); //task任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

void Result::setVal(Any any)
{
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();//已经获取到任务返回值，增加信号量资源
}