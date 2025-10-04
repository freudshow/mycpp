#include <iostream>
#include <unistd.h>
#include "timeWheel.h"
using namespace std;

void funccc(void)
{
    cout << "[" << __FUNCTION__ << "()" << "]"
            << "[" << __LINE__ << "]"
            << "exec function" << endl;
}

int main()
{
    TimeWheel wheel;
    wheel.initTimeWheel(100, 10);
    wheel.createTimingEvent(200, funccc);
    while (1)
    {
        sleep(1);
    }
}
