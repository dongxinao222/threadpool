#include "threadpool.h"
#include "iostream"


const int TASK_MAX_THRESHHOLD=INT32_MAX;
const int ThreadHoldMax_=12;
const int ThreadIdleMaxtime=10; //单位：秒
ThreadPool::ThreadPool()
    :initThreadSize_(4)
    ,taskSize_(0)
    ,taskQueMaxTheradHold_(TASK_MAX_THRESHHOLD)
    ,poolMode_(PoolMode::MODE_FIXED)
    ,isPoolRunning_(false)
    ,idleThreadSize_(0)
    ,curThreadSize_(0)
    ,ThreadThreshold_(ThreadHoldMax_)
{
}

ThreadPool::~ThreadPool()
{
    std::unique_lock<std::mutex> lock(queuelock_);
    isPoolRunning_ = false;
    notEmpty_.notify_all();
    Exit_.wait(lock, [this](){
        return curThreadSize_ == 0 && taskSize_ == 0;
    });
}

void ThreadPool::setMode(PoolMode mode)
{
    if(isRunning()) return;
    poolMode_=mode;
}

void ThreadPool::setTaskQueMaxTheradHold(int treshhold) //最大任务数
{
    if(isRunning()) return;
    if(poolMode_==PoolMode::MODE_CACHED)
    ThreadThreshold_=treshhold;
}
void ThreadPool::setThreadThreshold(int hold)
{
    if(isRunning()) return;
    taskQueMaxTheradHold_=hold;
}

Result ThreadPool::submitTask(std::shared_ptr<Task> task) //提交任务
{
    std::unique_lock<std::mutex> lock(queuelock_);
    //任务队列有空,最多阻塞一秒，超过返回失败
    if(notFull_.wait_for(lock,std::chrono::seconds(1),[&]()->bool {return taskSize_<(size_t)taskQueMaxTheradHold_;})==false)
    {
        printf("task queue is full,submit failed\n"); 
        return Result(task,false);
    }
    //任务队列放任务
    taskQueue_.emplace(task);
    taskSize_++;
    //放了任务绝对不空,通知一个线程取走任务
    notEmpty_.notify_all();
    //cache模式，如果任务多线程少就动态创建线程
    if(poolMode_==PoolMode::MODE_CACHED
        && taskQueue_.size()>idleThreadSize_
        && curThreadSize_<ThreadHoldMax_)
        {
            //创建新线程
            auto ptr=std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
            int threadId=ptr->getid();
            std::cout<<"创建线程："<<threadId<<std::endl;
            threads_.emplace(threadId,std::move(ptr));
            threads_[threadId]->start();
            idleThreadSize_++;
            curThreadSize_++;
        }

    return Result(task,true);
}

void ThreadPool::start(int size)
{
    initThreadSize_=size;
    //curThreadSize_=size;
    //idleThreadSize_=size;
    isPoolRunning_=true;

    for(size_t i=0;i<initThreadSize_;i++)
    {
        auto ptr=std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
        int threadId=ptr->getid();
        threads_.emplace(threadId,std::move(ptr));
    }

    for(size_t i=0;i<initThreadSize_;i++)
    {
        threads_[i]->start();
        idleThreadSize_++;
        curThreadSize_++;
    }
}

bool ThreadPool::isRunning() const
{
    return isPoolRunning_;
}
void ThreadPool::threadFunc(int threadid)
{
    auto lastTime=std::chrono::high_resolution_clock::now();
    while(true)
    {
        std::shared_ptr<Task> fn;
        {
            std::unique_lock<std::mutex> lock(queuelock_);
            printf("线程%d尝试获取任务...\n",gettid());

            // 修改：将退出检查移到 while 条件中
            while(taskSize_==0 && isRunning())
            {
                if(poolMode_==PoolMode::MODE_CACHED)
                {
                    if(std::cv_status::timeout ==
                    notEmpty_.wait_for(lock,std::chrono::seconds(1)))
                    {
                        auto now=std::chrono::high_resolution_clock::now();
                        auto dur=std::chrono::duration_cast<std::chrono::seconds>(now-lastTime);
                        if(dur.count()>=ThreadIdleMaxtime && curThreadSize_>initThreadSize_)
                        {
                            std::unique_ptr<Thread> t=std::move(threads_[threadid]);
                            threads_.erase(threadid);
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout<<"threadid:"<<threadid<<" deleted"<<std::endl;
                            return;
                        }
                    }
                }
                else
                {
                    notEmpty_.wait(lock);
                }
            }

            // 修改：增加这个关键检查！
            if(!isRunning() && taskSize_==0)
            {
                std::unique_ptr<Thread> t=std::move(threads_[threadid]);
                threads_.erase(threadid);
                curThreadSize_--;
                idleThreadSize_--;
                if(curThreadSize_==0) Exit_.notify_all();
                std::cout<<"threadid:"<<threadid<<" deleted"<<std::endl;
                return;
            }
            
            // 取任务执行
            idleThreadSize_--;
            fn=taskQueue_.front();taskQueue_.pop(); taskSize_--;
            printf("线程%d获取任务成功!\n",gettid());

            if(taskSize_>0)
            {
                notEmpty_.notify_one();
            }
            notFull_.notify_one();
        }
        
        if(fn != nullptr) fn->exec();
        idleThreadSize_++;
        lastTime=std::chrono::high_resolution_clock::now();
    }
}
/*void ThreadPool::threadFunc(int threadid)
{
    //printf("当前线程:%d\n",gettid());
    //printf("后线程:%d\n",gettid());
    auto lastTime=std::chrono::high_resolution_clock::now();
    while(true)
    {
        std::shared_ptr<Task> fn;
        {
            //获取任务队列锁
            std::unique_lock<std::mutex> lock(queuelock_);
            printf("线程%d尝试获取任务...\n",gettid());

            while(taskSize_==0)
            {
                if(!isRunning())
            {
                std::unique_ptr<Thread> t=std::move(threads_[threadid]);
                threads_.erase(threadid);
                curThreadSize_--;
                idleThreadSize_--;
                if(curThreadSize_==0) Exit_.notify_all();
                std::cout<<"threadid:"<<threadid<<" deleted"<<std::endl;
                return;
            }
                if(poolMode_==PoolMode::MODE_CACHED)
                {
                    if(std::cv_status::timeout ==
                    notEmpty_.wait_for(lock,std::chrono::seconds(1)))
                    {
                        auto now=std::chrono::high_resolution_clock::now();
                        auto dur=std::chrono::duration_cast<std::chrono::seconds>(now-lastTime);
                        if(dur.count()>=ThreadIdleMaxtime && curThreadSize_>initThreadSize_)
                        {
                            std::unique_ptr<Thread> t=std::move(threads_[threadid]);
                            threads_.erase(threadid);
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout<<"threadid:"<<threadid<<" deleted"<<std::endl;
                            return;
                        }
                    }
                }
                else
                {
                    //等待queEmpty条件
                    notEmpty_.wait(lock);
                }
            }

            
            idleThreadSize_--;
            //取一个人物出来并执行
            fn=taskQueue_.front();taskQueue_.pop(); taskSize_--;
            printf("线程%d获取任务成功!\n",gettid());

            if(taskSize_>0)
            {
                notEmpty_.notify_one();
            }

            notFull_.notify_one();
        }
        if(fn != nullptr) fn->exec();
        idleThreadSize_++;
        lastTime=std::chrono::high_resolution_clock::now();
    }
}*/


std::atomic_int Thread::generateid_=0;

Thread::Thread(ThreadFunc func)
    :func_(func)
    ,threadId_(generateid_++)
{}
Thread::~Thread(){}

void Thread::start()
{
    //创建一个线程执行一个线程函数
    std::thread t(func_,threadId_);
    t.detach();
}

int Thread::getid()
{
    return threadId_;
}

void Task::exec()
{
    auto i=run();
    if(result_)
    result_->setvalue(std::move(i));
}
void Task::setResult(Result* result)
{  
    result_=result;
}
Task::Task():result_(nullptr){}

class MyTask:public Task
{
private:
    uint32_t start_;
    uint32_t end_;
public:
    MyTask(int b,int e):start_(b),end_(e){}
    Any run()
    {
        //线程代码...
        /*printf("当前线程:%d\n",gettid());
        sleep(2);
        printf("后线程:%d\n",gettid());*/
        uint64_t sum=0;
        for(uint32_t i=start_;i<=end_;i++)
        {
            sum+=i;
        }
        printf("执行完成\n");
        return sum;
    }

};

Result::Result(std::shared_ptr<Task> task,bool isvalid):task_(task),isvalid_(isvalid)
{
    task_->setResult(this);
}
Result::~Result()
{
    task_->setResult(nullptr);
}
Any Result::get()
{
    if(!isvalid_) return "";
    sem_.wait(); //任务没执行完，需要阻塞
    return std::move(res_);
}

void Result::setvalue(Any any)
{
    this->res_ = std::move(any);
    sem_.post();
}
#include <iostream>
#include <chrono>
using namespace std;
int main()
{
    {

        ThreadPool tp;
        tp.setMode(PoolMode::MODE_CACHED);
        tp.start(4);
    //timeval s;
    //gettimeofday(&s,NULL);
    Result res=tp.submitTask(std::make_shared<MyTask>(1,1000000000));
    Result res1=tp.submitTask(std::make_shared<MyTask>(1,1000000000));
    Result res2=tp.submitTask(std::make_shared<MyTask>(1,1000000000));
    Result res3=tp.submitTask(std::make_shared<MyTask>(1,1000000000));
    tp.submitTask(std::make_shared<MyTask>(1,1000000000)).get();
    //cout<<(res.get().cast_<uint64_t>())<<endl;
    cout<<"main over"<<endl;
    //sleep(1);
}
    /*Result res2=tp.submitTask(std::make_shared<MyTask>(1000000001,2000000000));
    Result res3=tp.submitTask(std::make_shared<MyTask>(2000000001,3000000000));
    tp.submitTask(std::make_shared<MyTask>(2000000001,3000000000));
    tp.submitTask(std::make_shared<MyTask>(2000000001,3000000000));
    tp.submitTask(std::make_shared<MyTask>(2000000001,3000000000));
    
    
    cout<<(res.get().cast_<uint64_t>()+res2.get().cast_<uint64_t>()+res3.get().cast_<uint64_t>())<<endl;
    timeval e;
    gettimeofday(&e,NULL);
    cout<<(e.tv_sec-s.tv_sec)*1000000LL+(e.tv_usec-s.tv_usec)<<endl;
    
    
    gettimeofday(&s,NULL);
    int sum=0;
    for(uint64_t i=0;i<=3000000000;i++)
    {
        sum+=i;
    }
    gettimeofday(&e,NULL);
    cout<<(e.tv_sec-s.tv_sec)*1000000LL+(e.tv_usec-s.tv_usec)<<endl;
    //getchar();*/
    
    return 0;
}