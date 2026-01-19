#include <iostream>
#include <functional>
#include <future>
#include <thread>
#include <ThreadPool.h>
using namespace std;

int main()
{
    ThreadPool tp;
    //tp.setMode(PoolMode::MODE_CACHED);
    tp.start(2);
    auto f=tp.submitTask([](int a,int b){sleep(4);return a+b;},10,5);
    auto f1=tp.submitTask([](int a,int b){sleep(4);return a+b;},10,5);
    auto f2=tp.submitTask([](int a,int b){return a+b;},10,5);
    auto f4=tp.submitTask([](int a,int b){return a+b;},10,5);
    auto f5=tp.submitTask([](int a,int b){return a+b;},10,5);
    auto f6=tp.submitTask([](int a,int b){return a+b;},10,5);
    cout<<f.get()<<endl;
    cout<<f2.get()<<endl;
    cout<<f4.get()<<endl;
    cout<<f1.get()<<endl;
    cout<<f5.get()<<endl;
    cout<<f6.get()<<endl;
    return 0;
}