#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <timewheel.h>
#include <unistd.h>

static void get_local_time_buf(char *buf, size_t blen)
{
    if (blen < 24) return;
    struct timespec ts;
    struct tm tmv;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmv);
    snprintf(buf, blen, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000);
}

void debug_time_line(const char *fmt, ...)
{
    char timebuf[32] = {0};
    get_local_time_buf(timebuf, sizeof(timebuf));
    fprintf(stdout, "[%s][timewheel_c.c][]: ", timebuf);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
    fflush(stdout);
}

static uint32_t get_current_ms(TimePos_t pos, uint32_t steps)
{
    return steps * pos.pos_ms + pos.pos_sec * 1000 + pos.pos_min * 60 * 1000;
}

static void get_trigger_time_from_interval(TimeWheel *tw, uint32_t interval, TimePos_t *out, TimePos_t base)
{
    uint32_t cur = get_current_ms(base, tw->steps);
    uint32_t future = cur + interval;
    out->pos_min = (future / 1000 / 60) % tw->thirdLevelCount;
    out->pos_sec = (future % (1000 * 60)) / 1000;
    out->pos_ms = (future % 1000) / tw->steps;
}

static void insert_event_to_slot(TimeWheel *tw, uint32_t interval, Event_t *event, TimePos_t base)
{
    TimePos_t timePos = {0};
    get_trigger_time_from_interval(tw, interval, &timePos, base);

    if (timePos.pos_min != base.pos_min)
    {
        uint32_t idx = tw->firstLevelCount + tw->secondLevelCount + timePos.pos_min;
        event->next = tw->slotList[idx];
        tw->slotList[idx] = event;
    }
    else if (timePos.pos_sec != base.pos_sec)
    {
        uint32_t idx = tw->firstLevelCount + timePos.pos_sec;
        event->next = tw->slotList[idx];
        tw->slotList[idx] = event;
    }
    else if (timePos.pos_ms != base.pos_ms)
    {
        uint32_t idx = timePos.pos_ms;
        event->next = tw->slotList[idx];
        tw->slotList[idx] = event;
    }
}

static uint32_t process_event(TimeWheel *tw, Event_t **slotPtr, TimePos_t currentPos)
{
    Event_t *prev = NULL;
    Event_t *cur = *slotPtr;
    while (cur)
    {
        uint32_t currentMs = get_current_ms(currentPos, tw->steps);
        uint32_t lastProcessedMs = get_current_ms(cur->timePos, tw->steps);
        uint32_t period = tw->thirdLevelCount * 60 * 1000;
        uint32_t distanceMs = (currentMs + period - lastProcessedMs) % period;

        Event_t *next = cur->next;
        if (cur->interval == distanceMs)
        {
            // call
            cur->cb(cur->arg);
            // update last processed
            cur->timePos = currentPos;
            // remove from this slot (we'll re-insert)
            if (prev)
                prev->next = next;
            else
                *slotPtr = next;
            // re-insert with same interval
            insert_event_to_slot(tw, cur->interval, cur, currentPos);
        }
        else
        {
            // not due, just advance
            prev = cur;
        }
        cur = next;
    }
    return 0;
}

static inline void timespec_add_us(struct timespec *t, long us)
{
    t->tv_nsec += (us % 1000000) * 1000;
    t->tv_sec += us / 1000000;
    if (t->tv_nsec >= 1000000000)
    {
        t->tv_nsec -= 1000000000;
        t->tv_sec += 1;
    }
}

static void *loop_thread_fn(void *arg)
{
    TimeWheel *tw = (TimeWheel*) arg;
    if (!tw) return NULL;

    const long step_us = (long)tw->steps * 1000L;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t processedTicks = 0;

    while (tw->running)
    {
        // compute next tick target
        struct timespec target = start;
        uint64_t nextTick = processedTicks + 1;
        long addUs = (long)(nextTick * step_us);
        timespec_add_us(&target, addUs);
        // sleep until target
        // Sleep until target using nanosleep; compute remaining time loop to handle interrupts
        while (tw->running) {
            struct timespec now2;
            clock_gettime(CLOCK_MONOTONIC, &now2);
            long rem_us = (target.tv_sec - now2.tv_sec) * 1000000L + (target.tv_nsec - now2.tv_nsec) / 1000L;
            if (rem_us <= 0) break;
            struct timespec req;
            req.tv_sec = rem_us / 1000000L;
            req.tv_nsec = (rem_us % 1000000L) * 1000L;
            nanosleep(&req, NULL);
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_us = (now.tv_sec - start.tv_sec) * 1000000L + (now.tv_nsec - start.tv_nsec) / 1000L;
        if (elapsed_us < 0) continue;
        uint64_t ticksSinceStart = (uint64_t)(elapsed_us / step_us);
        if (ticksSinceStart < processedTicks + 1) continue;
        uint32_t totalPassed = (uint32_t)(ticksSinceStart - processedTicks);
        if (totalPassed == 0) continue;

        // process each intermediate tick
        TimePos_t prevPos = tw->timePos;
        uint32_t baseMs = get_current_ms(prevPos, tw->steps);
        for (uint32_t i = 1; i <= totalPassed; ++i)
        {
            TimePos_t pos = {0};
            uint32_t futureMs = baseMs + i * tw->steps;
            pos.pos_min = (futureMs / 1000 / 60) % tw->thirdLevelCount;
            pos.pos_sec = (futureMs % (1000 * 60)) / 1000;
            pos.pos_ms = (futureMs % 1000) / tw->steps;

            pthread_mutex_lock(&tw->mutex);
            if (pos.pos_min != prevPos.pos_min)
            {
                uint32_t idx = tw->firstLevelCount + tw->secondLevelCount + pos.pos_min;
                process_event(tw, &tw->slotList[idx], pos);
            }
            else if (pos.pos_sec != prevPos.pos_sec)
            {
                uint32_t idx = tw->firstLevelCount + pos.pos_sec;
                process_event(tw, &tw->slotList[idx], pos);
            }
            else if (pos.pos_ms != prevPos.pos_ms)
            {
                uint32_t idx = pos.pos_ms;
                process_event(tw, &tw->slotList[idx], pos);
            }
            pthread_mutex_unlock(&tw->mutex);

            prevPos = pos;
        }

        tw->timePos = prevPos;
        processedTicks += totalPassed;
    }

    return NULL;
}

void timewheel_init(TimeWheel *tw, uint32_t steps, uint32_t maxMin)
{
    if (!tw) return;
    if (1000 % steps != 0) { debug_time_line("invalid steps"); return; }
    tw->steps = steps;
    tw->firstLevelCount = 1000 / steps;
    tw->secondLevelCount = 60;
    tw->thirdLevelCount = maxMin;
    tw->increaseId = 0;
    tw->timePos.pos_min = tw->timePos.pos_sec = tw->timePos.pos_ms = 0;
    tw->running = 1;

    uint32_t totalSlots = tw->firstLevelCount + tw->secondLevelCount + tw->thirdLevelCount;
    tw->slotList = (Event_t**) calloc(totalSlots, sizeof(Event_t*));
    pthread_mutex_init(&tw->mutex, NULL);

    int rc = pthread_create(&tw->loopThread, NULL, loop_thread_fn, tw);
    if (rc != 0)
    {
        debug_time_line("create thread error: %s", strerror(rc));
    }
}

void timewheel_create_timing_event(TimeWheel *tw, uint32_t interval, EventCallback_t cb, void *arg)
{
    if (!tw) return;
    if (interval < tw->steps || interval % tw->steps != 0 || interval >= tw->steps * tw->firstLevelCount * tw->secondLevelCount * tw->thirdLevelCount)
    {
        debug_time_line("invalid interval");
        return;
    }

    Event_t *ev = (Event_t*) malloc(sizeof(Event_t));
    memset(ev, 0, sizeof(Event_t));
    ev->interval = interval;
    ev->cb = cb;
    ev->arg = arg;
    ev->timePos = tw->timePos;
    ev->id = tw->increaseId++;

    pthread_mutex_lock(&tw->mutex);
    insert_event_to_slot(tw, interval, ev, tw->timePos);
    pthread_mutex_unlock(&tw->mutex);
}

