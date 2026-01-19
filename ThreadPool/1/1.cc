#include <string>
#include <iostream>
using namespace std;
int main()
{
    char t[10];
    char n[10];
    sscanf("title:123 name:321","title:%10s name:%10s",t,n);
    char a[50];
    snprintf(a,50,"title:%s name:%s",t,n);
    printf("%s\n","10.1");
    cout<<a<<endl;
}