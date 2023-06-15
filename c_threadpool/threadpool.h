#ifndef _THREADPOLL_
#define _THREADPOLL_
#include <pthread.h>
#include<string.h>
#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>

//结构体主要保存信息有两类，一类线程池基本信息，一类各种权柄比如线程池id指向各个线程，指针指向任务队列
typedef struct ThreadPool ThreadPool;



//初始化线程池并返回一个线程池实例（申请一个结构体地址空间，初始化各种信息，并开辟相应权柄指向所需的空间）
ThreadPool* threadPoolCreate(int min,int max,int queueSize);
//销毁线程池时要回收资源所以管理者线程不能detach,工作线程可以detach


// 给线程池任务队列添加任务
void threadPoolAdd_task(ThreadPool* pool, void(*func)(void*), void* arg);


// 获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool* pool);
// 获取线程池中活着的线程的个数
int threadPoolAliveNum(ThreadPool* pool);
// 销毁线程池，将shutdown设置为1，工作线程（ 唤醒阻塞的）、管理线程、生产者线程检测后自行退出销毁，并回收资源
int threadPoolDestroy(ThreadPool* pool);



//////////////////////
// 工作的线程任务函数，创建工作线程时传给工作线程，使之成为消费者不断从任务队列取出任务执行
void* worker(void* arg);
// 管理者线程任务函数，创建工作线程时传给管理者线程，执行管理任务
void* manager(void* arg);
// 线程退出，释放的线程id为0，表明释放后该空间可以利用创建新线程
void threadExit(ThreadPool* pool);

#endif