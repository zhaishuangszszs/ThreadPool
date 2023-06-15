#include "threadpool.h"
const int NUMBER = 2;

// 任务结构体
typedef struct Task
{
    void (*function)(void *arg);
    void *arg;
} Task;

// 线程池结构体
struct ThreadPool
{
    // 任务队列
    Task *taskQ;
    int queueCapacity; // 容量
    int queueSize;     // 当前任务个数
    int queueFront;    // 队头->取数据
    int queueRear;     // 队尾->放数据

    pthread_t managerID;       // 管理者线程（一个）
    pthread_t *threadIDs;      // 工作的线程(不确定个数动态申请，所以为指针)
    int minNum;                // 最小线程数
    int maxNum;                // 最大线程数
    int busyNum;               // 忙的线程数
    int liveNum;               // 存活的线程数
    int exitNum;               // 要销毁的线程数
    pthread_mutex_t mutexPool; // 锁整个的线程池
    pthread_mutex_t mutexBusy; // 锁busyNum变量（busyNum经常访问，单独设置值锁提高效率）
    pthread_cond_t Full;       // 任务队列是不是满了，满了阻塞生产者
    pthread_cond_t Empty;      // 任务队列是不是空了，空阻塞消费者

    int shutdown; // 是否销毁线程池，销毁为1，否为0
};

//初始化线程池并返回一个线程池实例
ThreadPool *threadPoolCreate(int min, int max, int queueSize)
{

    // 1.线程池pool申请空间
    ThreadPool* pool = (ThreadPool *)malloc(sizeof(ThreadPool)); 

    do
    {
        if (pool == NULL)
        {
            printf("malloc threadpool fail...\n");
            break;
        }

    // 2.工作的线程申请空间
        pool->threadIDs = (pthread_t *)malloc(sizeof(pthread_t) * max); 
        if (pool->threadIDs == NULL)
        {
            printf("malloc threadIDs fail...\n");
            break;
        }
        memset(pool->threadIDs, 0, sizeof(pthread_t) * max); // 工作的线程空间初始化为0

    //3.参数初始化
        pool->minNum = min;
        pool->maxNum = max;
        pool->busyNum = 0;
        pool->liveNum = min; // 和最小个数相等
        pool->exitNum = 0;
        pool->shutdown = 0;

        if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
            pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
            pthread_cond_init(&pool->Empty, NULL) != 0 ||
            pthread_cond_init(&pool->Full, NULL) != 0) // 线程互斥锁条件变量初始化
        {
            printf("mutex or condition init fail...\n");
            break;
        }

    // 4.任务队列申请空间，并初始化相关信息
        pool->taskQ = (Task *)malloc(sizeof(Task) * queueSize); 
        pool->queueCapacity = queueSize;
        pool->queueSize = 0;
        pool->queueFront = 0;
        pool->queueRear = 0;


    // 5.创建线程实例

            //创建管理线程实例
        pthread_create(&pool->managerID, NULL, manager, pool);//传入管理任务函数
        printf("create manager thread %ld\n",pool->managerID);
            //创建工作线程实例
        for (int i = 0; i < min; ++i)//liveNum的线程
        {
            pthread_create(&pool->threadIDs[i], NULL, worker, pool);//传入工作任务函数成为消费者
            printf("create init thread %ld\n",pool->threadIDs[i]);
        }

    //6.返回线程池实例
        return pool;

    } while (0);

    // 7.出现问题释，放资源
    if (pool && pool->threadIDs)
        free(pool->threadIDs);
    if (pool && pool->taskQ)
        free(pool->taskQ);
    if (pool)
        free(pool);

    return NULL;
}

// 工作线程的任务函数（传给工作线程使工作线程成为消费者）
void *worker(void *arg)
{
    //1.传入线程池的参数
    ThreadPool *pool = (ThreadPool *)arg;
    
        //工作线程需要不停的运行
    while (1)
    {
    //2.锁住线程池
        pthread_mutex_lock(&pool->mutexPool);

    //3.当前任务队列是否为空，空则阻塞该工作线程
        while (pool->queueSize == 0 && !pool->shutdown)
        {
            // 为空阻塞该工作线程，并解锁线程池（注意：必须在while循环里）
            pthread_cond_wait(&pool->Empty, &pool->mutexPool);

            //唤醒会加锁
            //被唤醒可能是因为生产者又产生了任务，也有可能是管理者要销毁空闲进程

            // 判断是不是要销毁线程
            if (pool->exitNum > 0)
            {
                pool->exitNum--;                  // 先减，避免存活线程要小于最小线程
                if (pool->liveNum > pool->minNum) // 存活线程要大于最小线程
                {
                    pool->liveNum--;

                    threadExit(pool); // 将退出的线程id设为0(线程id属于线程池资源，修改应在锁内)
                    pthread_mutex_unlock(&pool->mutexPool);//解锁线程池
                    pthread_exit(NULL);//退出线程
                    //后两句可以加在threadExit()
                }
            }
        }

    //4.非空，判断线程池是否被关闭了
        if (pool->shutdown)//关闭的话，每个运行的工作线程自行判断，自己关闭
        {
            pool->liveNum--;
            threadExit(pool); // 将退出的线程id设为0
            pthread_mutex_unlock(&pool->mutexPool);//解锁线程池
            pthread_exit(NULL);//退出线程
        }

    //5.线程池非关闭，进行消费者任务
        // 从任务队列中取出一个任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.arg = pool->taskQ[pool->queueFront].arg;
        // 移动头结点
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
        pool->queueSize--;

    //6.取完任务，唤醒一个因任务满的生产者，解锁
        pthread_cond_signal(&pool->Full);       
        pthread_mutex_unlock(&pool->mutexPool); //解锁线程池

    //7.busyNum修改
        //busyNum修改需要加锁（修改busyNum）不需要锁住整个线程池
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        
    //8.工作线程调用工作任务函数进行工作
        printf("thread %ld start working...\n", pthread_self());
        //任务工作并发执行
        task.function(task.arg);
        free(task.arg);
        task.arg = NULL;
        printf("thread %ld end working...\n", pthread_self());
     
     //9.busyNum修改
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);
    }
    return NULL;
}

// 管理线程
void *manager(void *arg)
{
    //1.传入线程池的参数
    ThreadPool *pool = (ThreadPool *)arg;

        //当线程池不关闭时，管理线程需要不停的运行
    while (!pool->shutdown)
    {
        // 每隔3s检测一次（是否需要添加线程或者销毁线程）
        usleep(3);

    //2.取出线程池参数判断如何管理线程池

        // 取出线程池中任务的数量和当前线程的数量
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexPool);

    //3.判断是否添加线程（检测时刻）
        // 任务的个数+工作中的>存活的线程个数（工作会导致queuesize减一，加工作中的才是真正的queueSzie）&& 存活的线程数<最大线程数
        if (queueSize+busyNum> liveNum && liveNum < pool->maxNum)
        {
            //一次加两个
            pthread_mutex_lock(&pool->mutexPool); //线程池加锁
            int counter = 0;
            //遍历创建的工作线程地址空间寻找空闲地址，并创建新得工作线程
            for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i)
            {
                if (pool->threadIDs[i] == 0) // memset初始化为0和释放的线程id为0，说明空闲
                {
                    pthread_create(&pool->threadIDs[i], NULL, worker, pool);
                    printf("create new thread %ld\n",pool->threadIDs[i]);
                    counter++;
                    pool->liveNum++;//达到最大线程数后不在创建
                }
            }
            pthread_mutex_unlock(&pool->mutexPool); //线程池解锁
        }

    //4.判断是否销毁线程
        // 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
        if (busyNum * 2 < liveNum && liveNum > pool->minNum)
        {
            //一次销毁两个
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            // 让工作的线程自杀
            for (int i = 0; i < NUMBER; ++i)
            {
                pthread_cond_signal(&pool->Empty);//唤醒一个因任务空的线程自杀
            }
        }
    }//循环尾部

    return NULL;
}

// 给线程池任务队列添加任务（生产者）
void threadPoolAdd_task(ThreadPool *pool, void (*func)(void *), void *arg)
{
    //1.线程池加锁
    pthread_mutex_lock(&pool->mutexPool);
        //生产者不需要一直运行

    //2.判断任务队列是否满了（cond 必须while判断）
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        // 任务队列满了，阻塞生产者线程(并加锁)
        pthread_cond_wait(&pool->Full, &pool->mutexPool);
        //唤醒解锁
    }

    //3.判断线程池是否被关闭了
    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }

    //4.没关闭添加任务
    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;

    //5.唤醒一个因任务空等待线程
    pthread_cond_signal(&pool->Empty);

    //6.线程池解锁
    pthread_mutex_unlock(&pool->mutexPool);
}

void threadExit(ThreadPool *pool) // 将退出的线程id设为0
{
    pthread_t tid = pthread_self();
    for (int i = 0; i < pool->maxNum; ++i)
    {
        if (pool->threadIDs[i] == tid)
        {
            pool->threadIDs[i] = 0;
            printf("threadExit() called, %ld exiting...\n", tid);
            break;
        }
    }
    //threadExit(NULL);线程id属于线程池资源，修改应在锁内所以不能在这写
}

int threadPoolBusyNum(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutexBusy);
    int busyNum = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return busyNum;
}

int threadPoolAliveNum(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutexPool);
    int aliveNum = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return aliveNum;
}
int threadPoolDestroy(ThreadPool *pool)
{
    if (pool == NULL)
    {
        return -1;
    }

    //1. 关闭线程池,设置shutdown=1
    pthread_mutex_lock(&pool->mutexPool);
    pool->shutdown = 1;
    pthread_mutex_unlock(&pool->mutexPool);

    //2. 唤醒阻塞的消费者线程让其退出
    for (int i = 0; i < pool->liveNum; ++i)
    {
        pthread_cond_signal(&pool->Empty);
    }

    //3. 阻塞回收管理者线程
    pthread_join(pool->managerID, NULL);
    printf("manager thread %ld destroy\n",pool->managerID);

    //4.释放堆内存前要确保线程退出了
    while(threadPoolAliveNum(pool))
    {}

    //5.释放堆内存
    if (pool->taskQ)
    {
        free(pool->taskQ);
    }
    if (pool->threadIDs)
    {
        free(pool->threadIDs);
    }
    
    //6.销毁mutex和条件变量
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->Empty);
    pthread_cond_destroy(&pool->Full);
    
    //7.释放线程池地址空间
    free(pool);
    pool = NULL;

    return 0;
}