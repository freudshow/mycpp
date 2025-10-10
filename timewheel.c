#include "timewheel.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <libgen.h>

/* ==================== Utility Functions ==================== */

uint32_t get_local_time_ms(void)
{
    struct timeval systime;
    gettimeofday(&systime, NULL);
    return (systime.tv_sec * 1000 + systime.tv_usec / 1000);
}

void get_local_time(char *buf, uint32_t bufLen)
{
    if (bufLen < 24) {
        return;
    }

    struct timeval systime;
    struct tm timeinfo;

    gettimeofday(&systime, NULL);
    localtime_r(&systime.tv_sec, &timeinfo);
    snprintf(buf, bufLen, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
            (timeinfo.tm_year + 1900), timeinfo.tm_mon + 1,
            timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
            timeinfo.tm_sec, systime.tv_usec / 1000);
}

void debugBufFormat2fp(FILE *fp, const char *file, const char *func, int line, char *buf, int len, const char *fmt, ...)
{
    va_list ap;
    char bufTime[32] = { 0 };

    if (fp != NULL) {
        get_local_time(bufTime, sizeof(bufTime));
        fprintf(fp, "[%s][%s][%s()][%d]: ", bufTime, basename((char*) file), func, line);
        va_start(ap, fmt);
        vfprintf(fp, fmt, ap);
        va_end(ap);

        if (buf != NULL && len > 0) {
            int i = 0;
            for (i = 0; i < len; i++) {
                fprintf(fp, "%02X ", (uint8_t) buf[i]);
            }
        }

        fprintf(fp, "\n");
        fflush(fp);
        if (fp != stdout && fp != stderr)
            fclose(fp);
    }
}

/* ==================== Event List Operations ==================== */

static void eventlist_init(EventList_t *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

static void eventlist_push_back(EventList_t *list, Event_t *event)
{
    event->next = NULL;
    
    if (list->tail == NULL) {
        list->head = event;
        list->tail = event;
    } else {
        list->tail->next = event;
        list->tail = event;
    }
    list->count++;
}

static void eventlist_clear(EventList_t *list)
{
    Event_t *current = list->head;
    Event_t *next;
    
    while (current != NULL) {
        next = current->next;
        free(current);
        current = next;
    }
    
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

/* ==================== TimeWheel Internal Functions ==================== */

static uint32_t getCurrentMs(TimeWheel_t *wheel, TimePos_t timePos)
{
    return wheel->steps * timePos.pos_ms + timePos.pos_sec * 1000 + timePos.pos_min * 60 * 1000;
}

static uint32_t createEventId(TimeWheel_t *wheel)
{
    return wheel->increaseId++;
}

static void getTriggerTimeFromInterval(TimeWheel_t *wheel, uint32_t interval, TimePos_t *timePos, TimePos_t basePos)
{
    /* get current time: ms */
    uint32_t curTime = getCurrentMs(wheel, basePos);
    
    /* calculate which slot this interval should belong to */
    uint32_t futureTime = curTime + interval;
    timePos->pos_min = (futureTime / 1000 / 60) % wheel->thirdLevelCount;
    timePos->pos_sec = (futureTime % (1000 * 60)) / 1000;
    timePos->pos_ms = (futureTime % 1000) / wheel->steps;
}

static void insertEventToSlot(TimeWheel_t *wheel, uint32_t interval, Event_t *event, TimePos_t basePos)
{
    TimePos_t timePos = { 0 };
    
    /* calculate which slot this event should be set to */
    getTriggerTimeFromInterval(wheel, interval, &timePos, basePos);
    
    /* determine which level slot to insert */
    uint32_t slotIndex;
    if (timePos.pos_min != basePos.pos_min) {
        /* insert to minute slot */
        slotIndex = wheel->firstLevelCount + wheel->secondLevelCount + timePos.pos_min;
    } else if (timePos.pos_sec != basePos.pos_sec) {
        /* insert to second slot */
        slotIndex = wheel->firstLevelCount + timePos.pos_sec;
    } else if (timePos.pos_ms != basePos.pos_ms) {
        /* insert to millisecond slot */
        slotIndex = timePos.pos_ms;
    } else {
        /* should not happen, but handle it */
        return;
    }
    
    eventlist_push_back(&wheel->eventSlotArray.slots[slotIndex], event);
}

static uint32_t processEvent(TimeWheel_t *wheel, EventList_t *eventList, TimePos_t currentPos)
{
    Event_t *event = eventList->head;
    
    while (event != NULL) {
        /* calculate the current ms */
        uint32_t currentMs = getCurrentMs(wheel, currentPos);
        /* calculate last time(ms) this event was processed */
        uint32_t lastProcessedMs = getCurrentMs(wheel, event->timePos);
        
        /* calculate the distance between now and last time(ms) */
        uint32_t period = wheel->thirdLevelCount * 60 * 1000;
        uint32_t distanceMs = (currentMs + period - lastProcessedMs) % period;
        
        Event_t *next = event->next;
        
        /* if interval == distanceMs, need to process this event */
        if (event->interval == distanceMs) {
            /* process event */
            event->cb(event->arg);
            
            /* create a new event for next trigger */
            Event_t *newEvent = (Event_t *)malloc(sizeof(Event_t));
            if (newEvent != NULL) {
                memcpy(newEvent, event, sizeof(Event_t));
                newEvent->timePos = currentPos;
                newEvent->next = NULL;
                insertEventToSlot(wheel, event->interval, newEvent, currentPos);
            }
        } else {
            /* although this event in this position, but it not arriving timing */
            uint32_t remaining;
            if (event->interval > distanceMs) {
                remaining = event->interval - distanceMs;
            } else {
                remaining = event->interval + period - distanceMs;
            }
            
            /* create a new event with remaining time */
            Event_t *newEvent = (Event_t *)malloc(sizeof(Event_t));
            if (newEvent != NULL) {
                memcpy(newEvent, event, sizeof(Event_t));
                newEvent->next = NULL;
                insertEventToSlot(wheel, remaining, newEvent, currentPos);
            }
        }
        
        event = next;
    }
    
    return 0;
}

/* ==================== Loop Thread Function ==================== */

static void* loopForInterval(void *arg)
{
    if (arg == NULL) {
        DEBUG_TIME_LINE("invalid parameter");
        return NULL;
    }

    TimeWheel_t *wheel = (TimeWheel_t *)arg;
    
    /* Use CLOCK_MONOTONIC for steady time measurement */
    struct timespec startTime, nextTickTime;
    clock_gettime(CLOCK_MONOTONIC, &startTime);
    
    const int64_t stepNs = (int64_t)wheel->steps * 1000000LL;  /* nanoseconds per step */
    uint64_t processedTicks = 0;

    while (1) {
        /* Calculate next tick time based on anchor and processedTicks */
        int64_t nextTickNs = (int64_t)(processedTicks + 1) * stepNs;
        nextTickTime.tv_sec = startTime.tv_sec + nextTickNs / 1000000000LL;
        nextTickTime.tv_nsec = startTime.tv_nsec + nextTickNs % 1000000000LL;
        
        /* Normalize timespec */
        if (nextTickTime.tv_nsec >= 1000000000LL) {
            nextTickTime.tv_sec += nextTickTime.tv_nsec / 1000000000LL;
            nextTickTime.tv_nsec = nextTickTime.tv_nsec % 1000000000LL;
        }
        
        /* Sleep until next tick time (absolute time) */
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextTickTime, NULL) == EINTR) {
            /* Retry if interrupted */
        }

        /* Get current time to compute elapsed ticks */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        /* Calculate elapsed time in nanoseconds */
        int64_t elapsedNs = (int64_t)(now.tv_sec - startTime.tv_sec) * 1000000000LL +
                           (int64_t)(now.tv_nsec - startTime.tv_nsec);
        
        if (elapsedNs < 0) {
            continue;
        }

        uint64_t ticksSinceStart = (uint64_t)(elapsedNs / stepNs);
        if (ticksSinceStart < processedTicks + 1) {
            /* nothing new to process */
            continue;
        }

        uint32_t totalPassed = (uint32_t)(ticksSinceStart - processedTicks);
        if (totalPassed == 0) {
            continue;
        }

        /* Process each intermediate step so no slots are skipped */
        TimePos_t prevPos = wheel->timePos;
        uint32_t baseMs = getCurrentMs(wheel, prevPos);

        for (uint32_t i = 1; i <= totalPassed; ++i) {
            TimePos_t pos = { 0 };
            uint32_t futureMs = baseMs + i * wheel->steps;

            pos.pos_min = (futureMs / 1000 / 60) % wheel->thirdLevelCount;
            pos.pos_sec = (futureMs % (1000 * 60)) / 1000;
            pos.pos_ms = (futureMs % 1000) / wheel->steps;

            pthread_mutex_lock(&wheel->mutex);
            
            EventList_t *eventList = NULL;
            
            if (pos.pos_min != prevPos.pos_min) {
                eventList = &wheel->eventSlotArray.slots[wheel->firstLevelCount + wheel->secondLevelCount + pos.pos_min];
                processEvent(wheel, eventList, pos);
                eventlist_clear(eventList);
            } else if (pos.pos_sec != prevPos.pos_sec) {
                eventList = &wheel->eventSlotArray.slots[wheel->firstLevelCount + pos.pos_sec];
                processEvent(wheel, eventList, pos);
                eventlist_clear(eventList);
            } else if (pos.pos_ms != prevPos.pos_ms) {
                eventList = &wheel->eventSlotArray.slots[pos.pos_ms];
                processEvent(wheel, eventList, pos);
                eventlist_clear(eventList);
            }
            
            pthread_mutex_unlock(&wheel->mutex);
            
            prevPos = pos;
        }

        /* update wheel position and mark how many ticks we've processed */
        wheel->timePos = prevPos;
        processedTicks += totalPassed;
    }

    return NULL;
}

/* ==================== Public API Implementation ==================== */

TimeWheel_t* timewheel_create(uint32_t steps, uint32_t maxMin)
{
    TimeWheel_t *wheel = (TimeWheel_t *)malloc(sizeof(TimeWheel_t));
    if (wheel == NULL) {
        DEBUG_TIME_LINE("failed to allocate memory for timewheel");
        return NULL;
    }

    memset(wheel, 0, sizeof(TimeWheel_t));
    
    if (timewheel_init(wheel, steps, maxMin) != 0) {
        free(wheel);
        return NULL;
    }

    return wheel;
}

void timewheel_destroy(TimeWheel_t *wheel)
{
    if (wheel == NULL) {
        return;
    }

    /* Cancel and join the loop thread */
    pthread_cancel(wheel->loopThread);
    pthread_join(wheel->loopThread, NULL);

    /* Clear all event lists */
    if (wheel->eventSlotArray.slots != NULL) {
        for (uint32_t i = 0; i < wheel->eventSlotArray.size; i++) {
            eventlist_clear(&wheel->eventSlotArray.slots[i]);
        }
        free(wheel->eventSlotArray.slots);
    }

    pthread_mutex_destroy(&wheel->mutex);
    free(wheel);
}

int timewheel_init(TimeWheel_t *wheel, uint32_t steps, uint32_t maxMin)
{
    if (wheel == NULL) {
        DEBUG_TIME_LINE("invalid parameter: wheel is NULL");
        return -1;
    }

    if (1000 % steps != 0) {
        DEBUG_TIME_LINE("invalid steps: must be a factor of 1000");
        return -1;
    }

    wheel->steps = steps;
    wheel->firstLevelCount = 1000 / steps;
    wheel->secondLevelCount = 60;
    wheel->thirdLevelCount = maxMin;
    wheel->increaseId = 0;

    /* Allocate event slot array */
    wheel->eventSlotArray.size = wheel->firstLevelCount + wheel->secondLevelCount + wheel->thirdLevelCount;
    wheel->eventSlotArray.slots = (EventList_t *)malloc(sizeof(EventList_t) * wheel->eventSlotArray.size);
    if (wheel->eventSlotArray.slots == NULL) {
        DEBUG_TIME_LINE("failed to allocate memory for event slots");
        return -1;
    }

    /* Initialize all event lists */
    for (uint32_t i = 0; i < wheel->eventSlotArray.size; i++) {
        eventlist_init(&wheel->eventSlotArray.slots[i]);
    }

    /* Initialize mutex */
    if (pthread_mutex_init(&wheel->mutex, NULL) != 0) {
        DEBUG_TIME_LINE("failed to initialize mutex");
        free(wheel->eventSlotArray.slots);
        return -1;
    }

    /* Create loop thread */
    int ret = pthread_create(&wheel->loopThread, NULL, loopForInterval, wheel);
    if (ret != 0) {
        DEBUG_TIME_LINE("create thread error: %s", strerror(ret));
        pthread_mutex_destroy(&wheel->mutex);
        free(wheel->eventSlotArray.slots);
        return -1;
    }

    return 0;
}

int timewheel_create_event(TimeWheel_t *wheel, uint32_t interval, EventCallback_t callback, void *arg)
{
    if (wheel == NULL || callback == NULL) {
        DEBUG_TIME_LINE("invalid parameter");
        return -1;
    }

    if (interval < wheel->steps || interval % wheel->steps != 0 ||
        interval >= wheel->steps * wheel->firstLevelCount * wheel->secondLevelCount * wheel->thirdLevelCount) {
        DEBUG_TIME_LINE("invalid interval: %u", interval);
        return -1;
    }

    Event_t *event = (Event_t *)malloc(sizeof(Event_t));
    if (event == NULL) {
        DEBUG_TIME_LINE("failed to allocate memory for event");
        return -1;
    }

    memset(event, 0, sizeof(Event_t));
    event->interval = interval;
    event->cb = callback;
    event->arg = arg;
    event->timePos.pos_min = wheel->timePos.pos_min;
    event->timePos.pos_sec = wheel->timePos.pos_sec;
    event->timePos.pos_ms = wheel->timePos.pos_ms;
    event->id = createEventId(wheel);
    event->next = NULL;

    arg_t *pArg = (arg_t *)arg;
    if (pArg != NULL) {
        DEBUG_TIME_LINE("created event: id=%u, interval=%u, value=%u", pArg->id, pArg->interval, pArg->val);
    }

    /* Insert event to slot */
    pthread_mutex_lock(&wheel->mutex);
    insertEventToSlot(wheel, interval, event, wheel->timePos);
    pthread_mutex_unlock(&wheel->mutex);

    DEBUG_TIME_LINE("create event over");

    return 0;
}
