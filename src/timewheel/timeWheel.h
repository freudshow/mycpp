#include <memory>
#include <list>
#include <vector>
#include <mutex>
#include <pthread.h>
#include <stdint.h>
#include <atomic>
#include <thread>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <queue>
#include <condition_variable>

#define FILE_LINE       __FILE__,__FUNCTION__,__LINE__
#define DEBUG_BUFF_FORMAT(buf, bufSize, format, ...)    debugBufFormat2fp(stdout, FILE_LINE, (char*)buf, (int)bufSize, format, ##__VA_ARGS__)
#define DEBUG_TIME_LINE(format, ...)    DEBUG_BUFF_FORMAT(NULL, 0, format, ##__VA_ARGS__)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef struct TimePos {
        uint32_t pos_ms;
        uint32_t pos_sec;
        uint32_t pos_min;
} TimePos_t;
// keep old alias used in implementation
typedef TimePos_t TimePos;

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
        TimeWheel(uint32_t steps, uint32_t maxMin, bool runBackground = true);
        ~TimeWheel();

        void initTimeWheel(uint32_t steps, uint32_t maxMin, bool runBackground, size_t poolSize = 0);
        void setThreadPoolSize(size_t poolSize);
        void createTimingEvent(uint32_t interval, EventCallback_t callback, void *arg = NULL);
        void run(void);

        // new: enable/disable concurrent dispatch of events in a slot
        void setConcurrentDispatch(bool enable) { m_concurrentDispatch.store(enable); }

    private:
        static void* loopForInterval(void *arg);

        uint32_t getCurrentMs(TimePos_t timePos);
        uint32_t createEventId(void);
        // process events for a given slot
        uint32_t processEvent(std::list<Event_t> &eventList);
        // compute trigger time from interval
        void getTriggerTimeFromInterval(uint32_t interval, TimePos_t &timePos);
        // insertEventToSlot will use provided basePos as the reference time for placement
        void insertEventToSlot(uint32_t interval, Event_t &event);

        // helper to compare TimePos: returns -1 if a < b, 0 if equal, 1 if a > b
        static int compareTimePos(const TimePos_t &a, const TimePos_t &b) {
            if (a.pos_min < b.pos_min) return -1;
            if (a.pos_min > b.pos_min) return 1;
            if (a.pos_sec < b.pos_sec) return -1;
            if (a.pos_sec > b.pos_sec) return 1;
            if (a.pos_ms < b.pos_ms) return -1;
            if (a.pos_ms > b.pos_ms) return 1;
            return 0;
        }

        // simple thread-pool for dispatching event callbacks
        void startThreadPool(size_t workerCount);
        void stopThreadPool();
        void enqueueTask(std::function<void()> task);

        EventSlotList_t m_eventSlotList;
        TimePos_t m_timePos;
        TimePos_t m_lastTimePos;
        pthread_t m_loopThread;

        uint32_t m_firstLevelCount;
        uint32_t m_secondLevelCount;
        uint32_t m_thirdLevelCount;

        uint32_t m_steps;
        uint32_t m_increaseId;  // not used
        std::mutex m_mutex;
        std::atomic<bool> m_concurrentDispatch{false};

        // loop wake/stop helpers
        std::mutex m_loopCvMutex;
        std::condition_variable m_loopCv;
        std::atomic<size_t> m_desiredPoolSize{0};

        // threadpool members
        std::vector<std::thread> m_workers;
        std::queue<std::function<void()>> m_taskQueue;
        std::mutex m_taskMutex;
        std::condition_variable m_taskCv;
        std::atomic<bool> m_stopThreadPool{false};
        std::atomic<bool> m_stopLoop{false};
        size_t m_poolSize{0};
};

#ifdef __cplusplus
extern "C" {
#endif

    void debugBufFormat2fp(FILE *fp, const char *file, const char *func,
            int line, char *buf, int len, const char *fmt, ...);

    // runtime control for debug output from timeWheel implementation
    void setTimeWheelDebug(bool enable);

#ifdef __cplusplus
}
#endif