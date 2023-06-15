#include <pthread.h>
#include<stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include"threadpool.h"

void taskFunc(void* arg)
{
    int num=*(int*)arg;
    printf("thread %ld is working, number is %d,",pthread_self(),num);
    usleep(1000);
}

int main()
{
    ThreadPool* pool=threadPoolCreate(3, 10, 100);
    for(int i=0;i<100;i++)
    {
        int *num=(int*)malloc(sizeof(int));
        *num=i+100;
        threadPoolAdd_task(pool, taskFunc,num );
    }
    sleep(10);
    threadPoolDestroy(pool);
    return 0;
}