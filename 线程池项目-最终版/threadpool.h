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
const int THREAD_MAX_IDLE_TIME = 10;//��λ��

//�̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED,  //�̶������߳�
	MODE_CACHED, //�߳������ɶ�̬����
};

//�߳�����
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;
	//�̹߳��캯��
	Thread(ThreadFunc func)
		:func_(func)
		,threadId_(generatedId_++)
	{}
	//�߳���������
	~Thread() = default;
	//�����߳�
	void start()
	{
		//����һ���߳���ִ���̺߳���
	//ThreadPool::threadFunc() -> Thread���а󶨽��� -> func -> func_
		std::thread t(func_, threadId_);//c++11�������̶߳���t   ��  �߳�ִ�к���func_
		t.detach(); //���÷����߳�    �̶߳�����߳�ִ�к����ֿ��������̶߳���t�뿪������ᱻ������detach֮��Ӱ���̺߳�����ִ��
	}
	//��ȡ�߳�id
	int getId() const
	{
		return threadId_;
	}
private:

	ThreadFunc func_;
	int threadId_;//�����߳�id
	static int generatedId_;
};

int Thread::generatedId_ = 0;

//�̳߳�����
class ThreadPool
{
public:
	//�̳߳ع���
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

	//�̳߳�����
	~ThreadPool()
	{
		isPoolRunning_ = false;

		//notEmpty_.notify_all();//������Ϊû��Taskִ�У�������notEmpty_.wait_for�ϵ��̣߳����� -���ȴ�-������
		//�ȴ��̳߳�����̷߳���   ������״̬�� ���� & ����ִ��������

		std::unique_lock<std::mutex> lock(taskQueMtx_);

		notEmpty_.notify_all();

		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	//�����̵߳Ĺ���ģʽ
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
		{
			return;
		}
		poolMode_ = mode;
	}

	//���������߳�������ֵ
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

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
		{
			return;
		}
		taskQueMaxThreshHold_ = threshhold;
	}

	//���̳߳��ύ����
	//ʹ�ÿɱ��ģ���̣���submitTask ���Խ������������������������Ĳ���
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//������� �ŵ����������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//�߳�ͨ�ţ��ȴ���������п���  notFull_
		//�û��ύ�����������������1s,�����ж������ύʧ�ܣ�����
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			//��ʾnotFull_�ȴ�1s��������Ȼû������
			std::cerr << "task queue is full, submit task faill." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		//����п��࣬������ŵ����������
		taskQue_.emplace([task](){ (*task)(); });
		taskSize_++;

		//��Ϊ����������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ
		notEmpty_.notify_all();

		//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << ">>>>>create new thread...." << std::endl;
			//�������̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//�����߳�

			//�޸��̸߳�����ر���
			curThreadSize_++; //��ǰ�߳�����
			idleThreadSize_++;//�����߳�
		}

		//���������Result����
		//return task->getResult();
		return result;


	}

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//�����̳߳�����״̬
		isPoolRunning_ = true;

		//��¼��ʼ�̸߳���
		initThreadSize_ = initThreadSize;
		//��¼��ǰ�̸߳���
		curThreadSize_ = initThreadSize;

		//���д����̶߳���
		for (int i = 0; i < initThreadSize_; i++)
		{
			//����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}

		//���������߳�
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++;//��¼�����߳�����
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����߳���������
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		for (;;)
		{
			Task task;
			{
				//�Ȼ�ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;
				//cachedģʽ�£��п����Ѿ������˺ܶ���̡߳�
				//���ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳̽������յ�
				//����initThreadSize_�������߳�Ҫ���л���
				//��ǰʱ�� - ��һ���߳�ִ��ʱ�� > 60s
				//�� + ˫���ж� ��������
				while (taskQue_.size() == 0)
				{
					//�̳߳�Ҫ�����������߳���Դ
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
								//��ʼ���յ�ǰ�߳�
								//1.��¼�߳���������ر������б仯

								//2.���̶߳�����߳��б�������ɾ��
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
						//�ȴ�notEmpty����
						notEmpty_.wait(lock);
					}
				}

				idleThreadSize_--; //�����̼߳�1
				std::cout << "tid:" << std::this_thread::get_id() << "�����ȡ�ɹ�..." << std::endl;
				//�����������ȡһ���������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//�����ʣ�����񣬼���֪ͨ�����߳�ִ������
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				//ȡ����������֪ͨ,������������
				notFull_.notify_all();

			}//������������� �����ͷŵ�

			//��ǰ�̸߳���ִ���������
			if (task != nullptr)
			{
				//task->run();//ִ�����񣻰�ִ������ķ���ֵͨ��setVal��������Result
				task(); //using Task = std::function<void()> ���������packaged_task����õ�����
			}
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
		}
	}
	//���pool������״̬
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}
private:
	//std::vector<std::unique_ptr<Thread>> threads_; //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>>threads_; //�߳��б�
	int initThreadSize_; //��ʼ�߳�����
	std::atomic_int idleThreadSize_; //�����߳�����
	std::atomic_int curThreadSize_;//��¼��ǰ�߳�������
	int threadSizeThreshHold_; //�߳�����������ֵ

	using Task = std::function<void()>;
	std::queue<Task> taskQue_; //�������
	std::atomic_int taskSize_; //��������
	int taskQueMaxThreshHold_; //�������������ֵ

	std::mutex taskQueMtx_;//��֤��������̰߳�ȫ
	std::condition_variable notFull_; //��ʾ������в���
	std::condition_variable notEmpty_;//��ʾ������в���
	std::condition_variable exitCond_;//�ȴ��߳���Դȫ������

	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_; //��ʾ�̳߳صĹ���״̬

};


#endif

