#include <memory>
#include <list>
#include <vector>
#include <mutex>
#include <pthread.h>
#include <stdint.h>

#define FILE_LINE       __FILE__,__FUNCTION__,__LINE__
#define DEBUG_BUFF_FORMAT(buf, bufSize, format, ...)    debugBufFormat2fp(stdout, FILE_LINE, (char*)buf, (int)bufSize, format, ##__VA_ARGS__)
#define DEBUG_TIME_LINE(format, ...)    DEBUG_BUFF_FORMAT(NULL, 0, format, ##__VA_ARGS__)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef struct TimePos {
        uint32_t pos_ms;
        uint32_t pos_sec;
        uint32_t pos_min;
} TimePos_t;

typedef void (*EventCallback_t)(void*);

typedef struct Event {
        uint32_t id;
        EventCallback_t cb;
        void *arg;
        TimePos_t timePos;
        uint32_t interval;
} Event_t;

class TimeWheel;
typedef std::shared_ptr<TimeWheel> TimeWheelPtr;

typedef std::vector<std::list<Event_t>> EventSlotList_t;

class TimeWheel {
    public:
        TimeWheel();
        TimeWheel(uint32_t steps, uint32_t maxMin);

        void initTimeWheel(uint32_t steps, uint32_t maxMin);
        void createTimingEvent(uint32_t interval, EventCallback_t callback, void *arg = NULL);

    private:
        static void* loopForInterval(void *arg);

        uint32_t getCurrentMs(TimePos_t timePos);
        uint32_t createEventId(void);
        uint32_t processEvent(std::list<Event_t> &eventList);
        void getTriggerTimeFromInterval(uint32_t interval, TimePos_t &timePos);
        void insertEventToSlot(uint32_t interval, Event_t &event);

        EventSlotList_t m_eventSlotList;
        TimePos_t m_timePos;
        pthread_t m_loopThread;

        uint32_t m_firstLevelCount;
        uint32_t m_secondLevelCount;
        uint32_t m_thirdLevelCount;

        uint32_t m_steps;
        uint32_t m_increaseId;  // not used
        std::mutex m_mutex;
};

#ifdef __cplusplus
extern "C" {
#endif

    void debugBufFormat2fp(FILE *fp, const char *file, const char *func,
            int line, char *buf, int len, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

