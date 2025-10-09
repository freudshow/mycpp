#ifndef TIMEWHEEL_C_H
#define TIMEWHEEL_C_H

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

typedef struct TimePos {
    uint32_t pos_ms;
    uint32_t pos_sec;
    uint32_t pos_min;
} TimePos_t;

typedef void (*EventCallback_t)(void*);

typedef struct eventArg {
    uint32_t id;
    uint32_t val;
    uint32_t interval;
} arg_t;

typedef struct Event {
    uint32_t id;
    EventCallback_t cb;
    void *arg;
    TimePos_t timePos;
    uint32_t interval;
    struct Event *next; // linked list
} Event_t;

typedef struct TimeWheel {
    Event_t **slotList; // array of pointers to linked lists
    TimePos_t timePos;
    pthread_t loopThread;

    uint32_t firstLevelCount;
    uint32_t secondLevelCount; // = 60
    uint32_t thirdLevelCount;

    uint32_t steps;
    uint32_t increaseId;
    pthread_mutex_t mutex;
    int running;
} TimeWheel;

void debug_time_line(const char *fmt, ...);

void timewheel_init(TimeWheel *tw, uint32_t steps, uint32_t maxMin);
void timewheel_create_timing_event(TimeWheel *tw, uint32_t interval, EventCallback_t cb, void *arg);

#endif // TIMEWHEEL_C_H
