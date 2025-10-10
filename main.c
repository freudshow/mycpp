#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "timewheel.h"

#define SCHED_GRANULARITY       (100) /* ms */

arg_t g_funcArg[17] = { 0 };

void funccc(void *arg)
{
    if (arg == NULL) {
        DEBUG_TIME_LINE("arg: null");
        return;
    }

    arg_t *ev = (arg_t *)arg;
    ev->runCount++;

    ev->expectRunTime = ev->startTime + ev->runCount * ev->interval;
    uint32_t now = get_local_time_ms();
    uint32_t diff = now > ev->expectRunTime ? now - ev->expectRunTime : ev->expectRunTime - now;
    float error = ((float)diff / (float)ev->interval) * 100.0f;
    DEBUG_TIME_LINE("event[%u] runCount-%u, expectRunTime-%u, now-%u, diff-%u, interval-%u, error-%.2f%%",
            ev->id, ev->runCount, ev->expectRunTime, now, diff, ev->interval, error);
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
    TimeWheel_t *wheel = timewheel_create(SCHED_GRANULARITY, 10);
    if (wheel == NULL) {
        fprintf(stderr, "Failed to create timewheel\n");
        return -1;
    }

    printf("TimeWheel created successfully\n");
    printf("Creating %lu timing events...\n", ARRAY_SIZE(g_funcArg));

    for (uint32_t i = 0; i < ARRAY_SIZE(g_funcArg); i++) {
        g_funcArg[i].id = i + 1;
        g_funcArg[i].val = SCHED_GRANULARITY * (i + 1);
        g_funcArg[i].interval = SCHED_GRANULARITY * (i + 1);
        g_funcArg[i].startTime = get_local_time_ms();
        
        if (timewheel_create_event(wheel, SCHED_GRANULARITY * (i + 1), funccc, &g_funcArg[i]) != 0) {
            fprintf(stderr, "Failed to create event %u\n", i + 1);
        }
    }

    printf("All events created. Running...\n");

    while (1) {
        sleep(1);
    }

    /* Cleanup (never reached in this example) */
    timewheel_destroy(wheel);

    return 0;
}
