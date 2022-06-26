#include <iostream>
#include <chrono>
#include <thread>

#include "threadpool.h"

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
public:
	void run() { 线程代码.... }
};

pool.submitTask(std::make_shared<MyTask>());
*/

class MyTask : public Task
{
public:
	MyTask(int begin, int end) : begin_(begin), end_(end) {};
	//问题一：如何设计run函数返回值，可以表示任意类型
	Any run()
	{
		std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		int sum = 0;
		for (int i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		std::cout << "tid:" << std::this_thread::get_id() << "end!" << std::endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};

int main() {
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);
		//linux上 这些Result对象也是局部对象，要析构的！！！
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
	}//这里Result对象也要析构！！ 在vs下，条件变量析构会释放相应的资源
	std::cout << "main over!" << std::endl;
	std::getchar();

#if 0
	//问题： ThreadPool对象析构以后，怎样把线程池相关的线程资源全部回收？
	{
		ThreadPool pool;
		//用户自定义线程池模式
		pool.setMode(PoolMode::MODE_CACHED);

		pool.start(4);


		//问题二：如何设计result机制
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(11, 20));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(21, 30));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(31, 40));

		Result res5 = pool.submitTask(std::make_shared<MyTask>(41, 50));
		Result res6 = pool.submitTask(std::make_shared<MyTask>(51, 60));

		int sum1 = res1.get().cast<int>();//get返回Any类型，怎么转成具体的类型
		int sum2 = res2.get().cast<int>();
		int sum3 = res3.get().cast<int>();
		int sum4 = res4.get().cast<int>();
		int sum5 = res5.get().cast<int>();
		int sum6 = res6.get().cast<int>();

		int sum = sum1 + sum2 + sum3 + sum4 + sum5 + sum6;
		//Master-Slave线程模型
		//Master线程用来分解任务，然后各个Slave线程分配任务
		//等待各个Slave线程执行完任务，返回结果
		//Master线程合并各个任务结果，输出
		//std::this_thread::sleep_for(std::chrono::seconds(20));
		std::cout << "sum" << sum << std::endl;
	}

	std::getchar();
#endif

}