#ifndef TIMEWHEEL_H
#define TIMEWHEEL_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#define FILE_LINE       __FILE__,__FUNCTION__,__LINE__
#define DEBUG_BUFF_FORMAT(buf, bufSize, format, ...)    debugBufFormat2fp(stdout, FILE_LINE, (char*)buf, (int)bufSize, format, ##__VA_ARGS__)
#define DEBUG_TIME_LINE(format, ...)    DEBUG_BUFF_FORMAT(NULL, 0, format, ##__VA_ARGS__)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/* Time position in the wheel */
typedef struct TimePos {
        uint32_t pos_ms;
        uint32_t pos_sec;
        uint32_t pos_min;
} TimePos_t;

typedef void (*EventCallback_t)(void*); //Event callback function type
typedef void (*freeCallback_t)(void*);  //Free argument callback function type

/* Event argument structure */
typedef struct eventArg {
        uint64_t startTimeMs;
        uint64_t nextTimemMs;
        uint64_t expectTimeMs;
        uint32_t runCount;
        uint32_t id;
        uint32_t val;
        uint32_t interval;              // interval in milliseconds
} arg_t;

/* Event structure */
typedef struct Event {
        uint32_t id;
        EventCallback_t cb;
        arg_t *arg;
        freeCallback_t freeArgCb;
        TimePos_t timePos;
        uint32_t interval;
        struct Event *next; /* for linked list */
} Event_t;

/* Event list node (linked list) */
typedef struct EventList {
        Event_t *head;
        Event_t *tail;
        uint32_t count;
        pthread_t loopThread;
        pthread_mutex_t mutex;
} EventList_t;

/* Event slot array */
typedef struct EventSlotArray {
        EventList_t *slots;
        uint32_t size;
} EventSlotArray_t;

/* TimeWheel structure */
typedef struct TimeWheel {
        EventList_t eventList;
        EventSlotArray_t eventSlotArray; /* event slot array */
        TimePos_t timePos; /* current time position of wheel */
        pthread_t loopThread; /* thread for loop */

        uint32_t firstLevelCount; /* millisecond level */
        uint32_t secondLevelCount; /* second level, fixed to 60 */
        uint32_t thirdLevelCount; /* minute level */

        uint32_t steps; /* milliseconds of one tick */
        uint32_t increaseId; /* event id increase number */
        pthread_mutex_t mutex; /* mutex for event slot list */
} TimeWheel_t;

int eventListInit(EventList_t *eventList);
int eventList_addEvent(EventList_t *eventList, uint32_t interval, EventCallback_t callback, void *arg);
void eventList_destroy(EventList_t *eventList);
/* Public API functions */
TimeWheel_t* timewheel_create(uint32_t steps, uint32_t maxMin);
void timewheel_destroy(TimeWheel_t *wheel);
int timewheel_init(TimeWheel_t *wheel, uint32_t steps, uint32_t maxMin);
int timewheel_create_event(TimeWheel_t *wheel, uint32_t interval, EventCallback_t callback, void *arg);

/* Utility functions */
uint64_t get_ms_by_timesp(struct timespec *tp);
void get_local_time(char *buf, uint32_t bufLen);
void debugBufFormat2fp(FILE *fp, const char *file, const char *func,
        int line, char *buf, int len, const char *fmt, ...);

#endif /* TIMEWHEEL_H */
