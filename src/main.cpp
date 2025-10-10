#include <cstdint>
#include <iostream>
#include <unistd.h>
#include <libgen.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include "timeWheel.h"
using namespace std;

#define SCHED_GRANULARITY       (100) //ms

arg_t g_funcArg[17] = { 0 };

void funccc(void *arg)
{
    if (arg == NULL)
    {
        DEBUG_TIME_LINE("arg: null");
        return;
    }

    arg_t *ev = (arg_t*) arg;
    ev->runCount++;

    ev->expectRunTime = ev->startTime + ev->runCount * ev->interval;
    uint32_t now = get_local_time_ms();
    uint32_t diff = now > ev->expectRunTime ? now - ev->expectRunTime : ev->expectRunTime - now;
    float error = ((float) diff / (float) ev->interval) * 100.0f;
    DEBUG_TIME_LINE("event[%u] runCount-%u, expectRunTime-%u, now-%u, diff-%u, interval-%u, error-%f",
            ev->id, ev->runCount, ev->expectRunTime, now, diff, ev->interval, error);
}

int main(int argc, char *argv[])
{
    TimeWheel wheel(SCHED_GRANULARITY, 10);

    for (uint32_t i = 0; i < ARRAY_SIZE(g_funcArg); i++)
    {
        g_funcArg[i].id = i + 1;
        g_funcArg[i].val = SCHED_GRANULARITY * (i + 1);
        g_funcArg[i].interval = SCHED_GRANULARITY * (i + 1);
        g_funcArg[i].startTime = get_local_time_ms();
        wheel.createTimingEvent(SCHED_GRANULARITY * (i + 1), funccc, &g_funcArg[i]);
    }

    while (1)
    {
        sleep(1);
    }

    return 0;
}
