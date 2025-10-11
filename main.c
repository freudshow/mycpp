#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "timewheel.h"

#define SCHED_GRANULARITY       (100) /* ms */

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

    ev->expectTimeMs = ev->startTimeMs + ev->runCount * ev->interval;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    uint64_t nowMs = get_ms_by_timesp(&now);
    uint64_t diff = nowMs > ev->expectTimeMs ? nowMs - ev->expectTimeMs : ev->expectTimeMs - nowMs;
    float error = ((float) diff / (float) ev->interval) * 100.0f;
    DEBUG_TIME_LINE("event[%u] startTime-%llu runCount-%u, expectRunTime-%llu, now-%llu, diff-%llu, interval-%u, error-%.4f%%",
            ev->id, ev->startTimeMs, ev->runCount, ev->expectTimeMs, nowMs, diff, ev->interval, error);
}

int main(int argc, char *argv[])
{
    EventList_t *eventList = (EventList_t*) calloc(1, sizeof(EventList_t));
    if (eventList == NULL)
    {
        fprintf(stderr, "Failed to calloc eventList\n");
        return -1;
    }

    if (eventListInit(eventList) < 0)
    {
        free(eventList);
        fprintf(stderr, "Failed to init eventList\n");
        return -1;
    }

    printf("eventList created successfully\n");
    printf("Creating %lu timing events...\n", ARRAY_SIZE(g_funcArg));

    struct timespec now;
    for (uint32_t i = 0; i < ARRAY_SIZE(g_funcArg); i++)
    {
        g_funcArg[i].id = i + 1;
        g_funcArg[i].val = SCHED_GRANULARITY * (i + 1);
        g_funcArg[i].interval = SCHED_GRANULARITY * (i + 1);

        clock_gettime(CLOCK_REALTIME, &now);
        g_funcArg[i].startTimeMs = get_ms_by_timesp(&now);
        g_funcArg[i].nextTimemMs = g_funcArg[i].startTimeMs + g_funcArg[i].interval;

        eventList_addEvent(eventList, g_funcArg[i].interval, funccc, &g_funcArg[i]);
    }

    printf("All events created. Running...\n");

    while (1)
    {
        sleep(1);
    }

    /* Cleanup (never reached in this example) */
    eventList_destroy(eventList);

    return 0;
}
