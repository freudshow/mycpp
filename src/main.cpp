#include <iostream>
#include <unistd.h>
#include <libgen.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "timeWheel.h"
using namespace std;

#define FILE_LINE       __FILE__,__FUNCTION__,__LINE__
#define DEBUG_BUFF_FORMAT(buf, bufSize, format, ...)    debugBufFormat2fp(stdout, FILE_LINE, (char*)buf, (int)bufSize, format, ##__VA_ARGS__)
#define DEBUG_TIME_LINE(format, ...)    DEBUG_BUFF_FORMAT(NULL, 0, format, ##__VA_ARGS__)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define SCHED_FREQUENCE       (100) //ms

typedef struct eventArg {
        uint32_t id;
        uint32_t val;
        uint32_t interval;
} arg_t;

arg_t g_funcArg[100] = { 0 };

void get_local_time(char *buf, uint32_t bufLen)
{
    struct timeval systime;
    struct tm timeinfo;

    gettimeofday(&systime, NULL);
    localtime_r(&systime.tv_sec, &timeinfo);
    snprintf(buf, bufLen, "%04d-%02d-%02d %02d:%02d:%02d.%03ld", (timeinfo.tm_year + 1900), timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, systime.tv_usec / 1000);
}

void debugBufFormat2fp(FILE *fp, const char *file, const char *func, int line, char *buf, int len, const char *fmt, ...)
{
    va_list ap;
    char bufTime[25] = { 0 };

    if (fp != NULL)
    {
        get_local_time(bufTime, sizeof(bufTime));
        fprintf(fp, "[%s][%s][%s()][%d]: ", bufTime, basename((char*) file), func, line);
        va_start(ap, fmt);
        vfprintf(fp, fmt, ap);
        va_end(ap);

        if (buf != NULL && len > 0)
        {
            int i = 0;
            for (i = 0; i < len; i++)
            {
                fprintf(fp, "%02X ", (uint8_t) buf[i]);
            }
        }

        fprintf(fp, "\n");
        fflush(fp);
        if (fp != stdout && fp != stderr)
            fclose(fp);
    }
}

void funccc(void *arg)
{
    if (arg == NULL)
    {
        DEBUG_TIME_LINE("arg: null");
        return;
    }

    arg_t *ev = (arg_t*) arg;
    DEBUG_TIME_LINE("exec event[%u]: %u, interval: %u", ev->id, ev->val, ev->interval);
}

int main(int argc, char *argv[])
{
    TimeWheel wheel(SCHED_FREQUENCE, 10);

    for (uint32_t i = 0; i < ARRAY_SIZE(g_funcArg); i++)
    {
        g_funcArg[i].id = i;
        g_funcArg[i].val = SCHED_FREQUENCE * (i + 1);
        g_funcArg[i].interval = SCHED_FREQUENCE * (i + 1);
        wheel.createTimingEvent(SCHED_FREQUENCE * (i + 1), funccc, &g_funcArg[i]);
    }

    while (1)
    {
        sleep(1);
    }

    return 0;
}
