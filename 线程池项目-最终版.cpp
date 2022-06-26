// 线程池项目-最终版.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <thread>
#include <functional>
#include <future>
#include "threadpool.h"
using namespace std;

/*
一：如何让线程池提交任务更方便
    1.pool.submitTask(sum1, 10, 20);
      pool.submitTask(sum1, 10, 20);
      ***submitTask:可变参模板编程***
二：自己写的Result以及相关的类型，代码多
    c++11的线程库   thread 
                   packaged_task(内部有get_future()方法可以获得任务返回值) <==>   function函数对象（需要自己进行封装）
                   future<int> res = task.get_future() <==> Result res = submitTask(); + 信号量的实现 + Any类实现
                   async
    ***使用future来替代Result 节省线程池代码***
*/
int sum1(int a, int b)
{
    return a + b;
}
int sum2(int a, int b, int c)
{
    return a + b + c;
}

int main()
{
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(4);

    future<int> res1 = pool.submitTask(sum1, 1, 2);
    future<int> res2 = pool.submitTask(sum2, 1, 2, 3);
    future<int> res3 = pool.submitTask([](int begin, int end)->int {
        int sum = 0;
        for(int i = begin; i <= end; i++)
        {
            sum += i;
        }
        return sum;
        }, 1, 100);
    future<int> res4 = pool.submitTask(sum1, 1, 2);
    future<int> res5 = pool.submitTask(sum1, 1, 2);

    cout << res1.get() << endl;
    cout << res2.get() << endl;
    cout << res3.get() << endl;
    cout << res4.get() << endl;
    cout << res5.get() << endl;

}
