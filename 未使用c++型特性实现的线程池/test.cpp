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
	void run() { �̴߳���.... }
};

pool.submitTask(std::make_shared<MyTask>());
*/

class MyTask : public Task
{
public:
	MyTask(int begin, int end) : begin_(begin), end_(end) {};
	//����һ��������run��������ֵ�����Ա�ʾ��������
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
		//linux�� ��ЩResult����Ҳ�Ǿֲ�����Ҫ�����ģ�����
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
		pool.submitTask(std::make_shared<MyTask>(1, 10));
	}//����Result����ҲҪ�������� ��vs�£����������������ͷ���Ӧ����Դ
	std::cout << "main over!" << std::endl;
	std::getchar();

#if 0
	//���⣺ ThreadPool���������Ժ��������̳߳���ص��߳���Դȫ�����գ�
	{
		ThreadPool pool;
		//�û��Զ����̳߳�ģʽ
		pool.setMode(PoolMode::MODE_CACHED);

		pool.start(4);


		//�������������result����
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(11, 20));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(21, 30));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(31, 40));

		Result res5 = pool.submitTask(std::make_shared<MyTask>(41, 50));
		Result res6 = pool.submitTask(std::make_shared<MyTask>(51, 60));

		int sum1 = res1.get().cast<int>();//get����Any���ͣ���ôת�ɾ��������
		int sum2 = res2.get().cast<int>();
		int sum3 = res3.get().cast<int>();
		int sum4 = res4.get().cast<int>();
		int sum5 = res5.get().cast<int>();
		int sum6 = res6.get().cast<int>();

		int sum = sum1 + sum2 + sum3 + sum4 + sum5 + sum6;
		//Master-Slave�߳�ģ��
		//Master�߳������ֽ�����Ȼ�����Slave�̷߳�������
		//�ȴ�����Slave�߳�ִ�������񣬷��ؽ��
		//Master�̺߳ϲ����������������
		//std::this_thread::sleep_for(std::chrono::seconds(20));
		std::cout << "sum" << sum << std::endl;
	}

	std::getchar();
#endif

}