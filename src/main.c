#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <timewheel.h>

#define SCHED_FREQUENCE (100)

arg_t g_funcArg[17];

void funccc(void *arg)
{
    if (!arg) { debug_time_line("arg: null"); return; }
    arg_t *ev = (arg_t*) arg;
    debug_time_line("exec event[%u]: value-%u, interval-%u", ev->id, ev->val, ev->interval);
}

int main(int argc, char **argv)
{
    TimeWheel tw;
    memset(&tw, 0, sizeof(tw));
    timewheel_init(&tw, SCHED_FREQUENCE, 10);

    for (uint32_t i = 0; i < 17; ++i)
    {
        g_funcArg[i].id = i + 1;
        g_funcArg[i].val = SCHED_FREQUENCE * (i + 1);
        g_funcArg[i].interval = SCHED_FREQUENCE * (i + 1);
        timewheel_create_timing_event(&tw, g_funcArg[i].interval, funccc, &g_funcArg[i]);
    }

    sleep(20);
    // stop
    tw.running = 0;
    // give thread a moment
    sleep(1);
    return 0;
}
