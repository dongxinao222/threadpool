#pragma once

#include <vector>
#include <unistd.h>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <sys/time.h>
#include <unordered_map>
#include <iostream>
#include <future>


const int TASK_MAX_THRESHHOLD=INT32_MAX;
const int ThreadHoldMax_=2;
const int ThreadIdleMaxtime=10; //单位：秒

enum class PoolMode
{
    MODE_FIXED,
    MODE_CACHED,
};

class Thread
{
public:
using ThreadFunc=std::function<void(int)>;
    Thread(ThreadFunc func)
    :func_(func)
    ,threadId_(generateid_++)
{}
    ~Thread()=default;
    void start()
    {
        //创建一个线程执行一个线程函数
        std::thread t(func_,threadId_);
        t.detach();
    }
    int getid()
    {
        return threadId_;
    }
private:
    ThreadFunc func_;
    static std::atomic_int generateid_;
    int threadId_;
};
std::atomic_int Thread::generateid_=0;
class ThreadPool
{
public:
    ThreadPool()
    :initThreadSize_(4)
    ,taskSize_(0)
    ,taskQueMaxTheradHold_(TASK_MAX_THRESHHOLD)
    ,poolMode_(PoolMode::MODE_FIXED)
    ,isPoolRunning_(false)
    ,idleThreadSize_(0)
    ,curThreadSize_(0)
    ,ThreadThreshold_(ThreadHoldMax_)
    {}
    ~ThreadPool()
    {
        std::unique_lock<std::mutex> lock(queuelock_);
        isPoolRunning_ = false;
        notEmpty_.notify_all();
        Exit_.wait(lock, [this](){
        return curThreadSize_ == 0 && taskSize_ == 0;
    });
    }

    void setMode(PoolMode mode)
    {
        if(isRunning()) return;
        poolMode_=mode;
    }
    void setTaskQueMaxTheradHold(int treshhold)
    {
        if(isRunning()) return;
        if(poolMode_==PoolMode::MODE_CACHED)
        ThreadThreshold_=treshhold;
    }
    void setThreadThreshold(int hold)
    {
        if(isRunning()) return;
        taskQueMaxTheradHold_=hold;
    }

    template <class T,class... Args>
    decltype(auto) submitTask(T&& f,Args&&... args) //-> std::future<decltype(f(args...))>
    {
        using RType = decltype(f(args...));
        auto task =std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<T>(f),std::forward<Args>(args)...)
        );
        //lambda版本可读性差
        //auto task = std::make_shared<std::packaged_task<RType()>>(
        //                [f=std::forward<T>(f),args=std::make_tuple(std::forward<Args>(args))]
        //                    {return std::apply(f,args)}
        //);
        auto result= task->get_future();

        std::unique_lock<std::mutex> lock(queuelock_);
        //任务队列有空,最多阻塞一秒，超过返回失败
        if(notFull_.wait_for(lock,std::chrono::seconds(1),[&]()->bool 
                {return taskSize_<(size_t) ThreadThreshold_;})==false)
        {
            printf("task queue is full,submit failed\n"); 
            auto task =std::make_shared<std::packaged_task<RType()>>(
            []()->RType{return RType();});
            (*task)();
            return task->get_future();
        }
        //任务队列放任务
        taskQueue_.emplace([task](){(*task)();});
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
        return result;
    }
    void start(int size=std::thread::hardware_concurrency())
    {
        initThreadSize_=size;
        //idleThreadSize_=size;
        //curThreadSize_=size;
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

    ThreadPool(ThreadPool&)=delete;
    ThreadPool&operator =(ThreadPool&) =delete;
private:
    void threadFunc(int threadid)
    {
        auto lastTime=std::chrono::high_resolution_clock::now();
    while(true)
    {
        std::function<void()> fn;
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
        
        if(fn != nullptr) fn();
        idleThreadSize_++;
        lastTime=std::chrono::high_resolution_clock::now();
    }
    }
    bool isRunning() const
    {
        return isPoolRunning_;
    }
private:
    PoolMode poolMode_;
    //std::vector<std::unique_ptr<Thread>> threads_;
    std::unordered_map<int,std::unique_ptr<Thread>> threads_;
    size_t initThreadSize_;
    std::atomic_int curThreadSize_;
    std::atomic_int idleThreadSize_;

    int taskQueMaxTheradHold_;
    int ThreadThreshold_;

    std::mutex queuelock_;
    using Task=std::function<void()>;
    std::queue<Task> taskQueue_;
    std::atomic_uint taskSize_;

    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable Exit_;
    std::atomic_bool isPoolRunning_;
};