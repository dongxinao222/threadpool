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


class noncopyable
{
public:
    noncopyable(const noncopyable&)=delete;
    noncopyable& operator =(const noncopyable&)=delete;
protected:
    noncopyable()=default;
    ~noncopyable()=default;
};

class Any : noncopyable
{
public:
Any()=default;
~Any()=default;
/*
Any(Any&)=delete;
Any& operator =(Any&)=delete;
*/
    Any(Any&&)=default;
    Any& operator =(Any&&)=default;
    template<class T>
    Any(T data):base_(std::make_unique<Derive<T>>(data)){}

    template<class T>
    T cast_()
    {
        Derive<T> *pd=dynamic_cast<Derive<T>*>(base_.get());
        if(pd==nullptr) 
        {
            throw "type cast unmatch!";
        }
        else
        {
            return pd->data_;
        }
    }
    template<class T>
    T* castptr_()
    {
        Derive<T> *pd=dynamic_cast<Derive<T>*>(base_.get());
        if(pd==nullptr) 
        {
            throw "type cast unmatch!";
        }
        else
        {
            return &pd->data_;
        }
    }
private:
    class Base
    {
    public:
        virtual ~Base() = default;
    };

    template<class T>
    class Derive: public Base
    {
    public:
        Derive(T data):data_(data){}
        T data_;
    };

private:
    std::unique_ptr<Base> base_;
};

class Semaphore //countdown_latch
{
private:
    std::mutex mmutex_;
    std::condition_variable cond_;
    uint reslimit_;
public:
    Semaphore(int limit=0):reslimit_(limit){}
    ~Semaphore()=default;
    void wait() //p操作
    {
        std::unique_lock<std::mutex> lock(mmutex_);
        cond_.wait(lock,[&]{return reslimit_>0;});
        reslimit_--;
    }
    void post() //v操作
    {
        std::unique_lock<std::mutex> lock(mmutex_);
        reslimit_++;
        cond_.notify_all();
    }    
};

class Task;
class Result
{
public:
    Result(std::shared_ptr<Task> task,bool isvalid=true);
    ~Result();
    
    void setvalue(Any any);
    Any get();
private:
    Any res_;
    Semaphore sem_;
    std::shared_ptr<Task> task_;  //指向对应获取返回值的任务对象
    std::atomic_bool isvalid_;
};

class Task
{
public:
    Task();
    ~Task()=default;
    void exec();
    void setResult(Result* result);
    virtual Any run()=0;
private:
    Result* result_;
};


enum class PoolMode
{
    MODE_FIXED,
    MODE_CACHED,
};

class Thread
{
public:
using ThreadFunc=std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread();
    void start();
    int getid();
private:
    ThreadFunc func_;
    static std::atomic_int generateid_;
    int threadId_;
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask:public Task
{
public:
    void run()
    {
        //线程代码...
    }

};

pool.submitTask(std::make_shared<MyTask>());
*/

class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    void setMode(PoolMode mode);
    void setTaskQueMaxTheradHold(int treshhold);
    void setThreadThreshold(int hold);

    Result submitTask(std::shared_ptr<Task> task);
    //void setInitThreadSize(int size);

    void start(int size=std::thread::hardware_concurrency());

    ThreadPool(ThreadPool&)=delete;
    ThreadPool&operator =(ThreadPool&) =delete;
private:
    void threadFunc(int thread);
    bool isRunning() const;
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
    std::queue<std::shared_ptr<Task>> taskQueue_;
    std::atomic_uint taskSize_;

    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable Exit_;
    std::atomic_bool isPoolRunning_;
};