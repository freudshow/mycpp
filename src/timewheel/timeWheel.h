#include <memory>
#include <list>
#include <vector>
#include <mutex>

typedef struct TimePos {
        int pos_ms;
        int pos_sec;
        int pos_min;
} TimePos_t;

typedef struct Event {
        int id;
        void (*cb)(void);
        void *arg;
        TimePos_t timePos;
        int interval;
} Event_t;

class TimeWheel {
        typedef std::shared_ptr<TimeWheel> TimeWheelPtr;
        typedef void (*EventCallback_t)(void);
        typedef std::vector<std::list<Event_t>> EventSlotList_t;
        public:
        TimeWheel();

        void initTimeWheel(int steps, int maxMin);
        void createTimingEvent(int interval, EventCallback_t callback);

    public:
        static void* loopForInterval(void *arg);

    private:
        int getCurrentMs(TimePos_t timePos);
        int createEventId();
        int processEvent(std::list<Event_t> &eventList);
        void getTriggerTimeFromInterval(int interval, TimePos_t &timePos);
        void insertEventToSlot(int interval, Event_t &event);

        EventSlotList_t m_eventSlotList;
        TimePos_t m_timePos;
        pthread_t m_loopThread;

        int m_firstLevelCount;
        int m_secondLevelCount;
        int m_thirdLevelCount;

        int m_steps;
        int m_increaseId;  // not used
        std::mutex m_mutex;
};
