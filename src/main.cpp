#include <cstdint>
#include <iostream>
#include <unistd.h>
#include <libgen.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "timeWheel.h"
using namespace std;

#define SCHED_FREQUENCE       (100) //ms

arg_t g_funcArg[17] = { 0 };

void funccc(void *arg)
{
    if (arg == NULL)
    {
        DEBUG_TIME_LINE("arg: null");
        return;
    }

    arg_t *ev = (arg_t*) arg;
    DEBUG_TIME_LINE("exec event[%u]: value-%u, interval-%u", ev->id, ev->val, ev->interval);
}

int main(int argc, char *argv[])
{
    TimeWheel wheel(SCHED_FREQUENCE, 10);

    for (uint32_t i = 0; i < ARRAY_SIZE(g_funcArg); i++)
    {
        g_funcArg[i].id = i + 1;
        g_funcArg[i].val = SCHED_FREQUENCE * (i + 1);
        g_funcArg[i].interval = SCHED_FREQUENCE * (i + 1);
        wheel.createTimingEvent(SCHED_FREQUENCE * (i + 1), funccc, &g_funcArg[i]);
    }

    uint16_t count = 30;
    while (count--)
    {
        sleep(1);
    }

    return 0;
}
