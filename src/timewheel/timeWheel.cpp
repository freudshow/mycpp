#include "timeWheel.h"
#include <sys/time.h>
#include <iostream>
#include <memory.h>
#include <chrono>
#include <thread>
#include <stdarg.h>
#include <libgen.h>
#include <errno.h>
#include <atomic>

static std::atomic<bool> g_enable_debug{false};

void get_local_time(char *buf, uint32_t bufLen)
{
    if (bufLen < 24)
    {
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
    if (!g_enable_debug.load()) return;
    va_list ap;
    char bufTime[32] = { 0 };

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

TimeWheel::TimeWheel()
{
    m_steps = 0;
    m_firstLevelCount = 0;
    m_secondLevelCount = 60;
    m_thirdLevelCount = 0;
    m_increaseId = 0;
    m_loopThread = 0;
    memset(&m_timePos, 0, sizeof(m_timePos));
    memset(&m_lastTimePos, 0, sizeof(m_lastTimePos));
}

TimeWheel::TimeWheel(uint32_t steps, uint32_t maxMin, bool runBackground)
{
    m_steps = 0;
    m_firstLevelCount = 0;
    m_secondLevelCount = 60;
    m_thirdLevelCount = 0;
    m_increaseId = 0;
    m_loopThread = 0;
    memset(&m_timePos, 0, sizeof(m_timePos));

    initTimeWheel(steps, maxMin, runBackground);
}

/*********************************************************
 * loop thread function
 * -------------------------------------------------------
 * @param[in] arg - TimeWheel object pointer
 * -------------------------------------------------------
 * @return: void* nullptr
 ********************************************************/
void* TimeWheel::loopForInterval(void *arg)
{
    if (arg == NULL)
    {
        DEBUG_TIME_LINE("valid parameter");
        return NULL;
    }

    TimeWheel *timeWheel = reinterpret_cast<TimeWheel*>(arg);
    while (!timeWheel->m_stopLoop.load())
    {
        // Wait for either the timeout (m_steps) or a stop notification
        std::unique_lock<std::mutex> lk(timeWheel->m_loopCvMutex);
        timeWheel->m_loopCv.wait_for(lk, std::chrono::milliseconds(timeWheel->m_steps), [timeWheel]() { return timeWheel->m_stopLoop.load(); });

        TimePos pos = { 0 };
        timeWheel->m_lastTimePos = timeWheel->m_timePos;
        //update slot of current TimeWheel
        timeWheel->getTriggerTimeFromInterval(timeWheel->m_steps, pos);
        timeWheel->m_timePos = pos;

        // Determine which slot to process and swap its list out under lock
        std::list<Event_t> toProcess;
        {
            std::unique_lock<std::mutex> lock(timeWheel->m_mutex);
            // if minute changed, process in integral point (minute)
            if (pos.pos_min != timeWheel->m_lastTimePos.pos_min)
            {
                // DEBUG_TIME_LINE("minutes changed");
                size_t idx = timeWheel->m_timePos.pos_min + timeWheel->m_firstLevelCount + timeWheel->m_secondLevelCount;
                toProcess.swap(timeWheel->m_eventSlotList[idx]);
            }
            else if (pos.pos_sec != timeWheel->m_lastTimePos.pos_sec)
            {
                //in same minute, but second changed, now is in this integral second
                // DEBUG_TIME_LINE("second changed");
                size_t idx = timeWheel->m_timePos.pos_sec + timeWheel->m_firstLevelCount;
                toProcess.swap(timeWheel->m_eventSlotList[idx]);
            }
            else if (pos.pos_ms != timeWheel->m_lastTimePos.pos_ms)
            {
                //now in this ms
                // DEBUG_TIME_LINE("ms changed");
                size_t idx = timeWheel->m_timePos.pos_ms;
                toProcess.swap(timeWheel->m_eventSlotList[idx]);
            }
            // DEBUG_TIME_LINE("loop over");
        }

        if (!toProcess.empty())
            timeWheel->processEvent(toProcess);
    }

    return nullptr;
}

/***************************************************************
 * init TimeWheel's step and maxmin,
 * which detemine the max period of this wheel
 * -------------------------------------------------------------
 * @param[in] steps - step of ms, should be the factor of 1000
 * @param[in] maxMin - max minutes this wheel can support
 * -------------------------------------------------------------
 * @return: void* nullptr
 **************************************************************/
void TimeWheel::initTimeWheel(uint32_t steps, uint32_t maxMin, bool runBackground, size_t poolSize)
{
    if (1000 % steps != 0)
    {
        DEBUG_TIME_LINE("invalid steps");
        return;
    }

    // record desired pool size
    if (poolSize > 0) m_desiredPoolSize.store(poolSize);

    m_steps = steps;
    m_firstLevelCount = 1000 / steps;
    m_thirdLevelCount = maxMin;

    m_eventSlotList.resize(m_firstLevelCount + m_secondLevelCount + m_thirdLevelCount);

    // start a thread pool for dispatching callbacks. If a pool size was provided via m_desiredPoolSize, use it.
    size_t workers = m_desiredPoolSize.load();
    if (workers == 0) workers = std::max<size_t>(1, std::thread::hardware_concurrency());
    startThreadPool(workers);

    if (runBackground)
    {
        uint32_t ret = pthread_create(&m_loopThread, NULL, loopForInterval, this);
        if (ret != 0)
        {
            DEBUG_TIME_LINE("create thread error:%s", strerror(errno));
            return;
        }
    }
// pthread_join(m_loopThread, NULL);
}

/***************************************************************
 * create a timing event, which will be trigger in interval ms
 * -------------------------------------------------------------
 * @param[in] interval - interval of ms,
 *                       should be the factor of steps
 * @param[in] callback - max minutes this wheel can support
 * -------------------------------------------------------------
 * @return: no return
 **************************************************************/
void TimeWheel::createTimingEvent(uint32_t interval, EventCallback_t callback, void *arg)
{
    if (interval < m_steps || interval % m_steps != 0 || interval >= m_steps * m_firstLevelCount * m_secondLevelCount * m_thirdLevelCount)
    {
        DEBUG_TIME_LINE("invalid interval");
        return;
    }

    DEBUG_TIME_LINE("start create event");
    Event_t event = { 0 };
    event.interval = interval;
    event.cb = callback;
    event.arg = arg;
//set time start
    event.timePos.pos_min = m_timePos.pos_min;
    event.timePos.pos_sec = m_timePos.pos_sec;
    event.timePos.pos_ms = m_timePos.pos_ms;
    event.id = createEventId();
// insert it to a slot of TimeWheel
    std::unique_lock<std::mutex> lock(m_mutex);
    insertEventToSlot(interval, event);
    DEBUG_TIME_LINE("create over");
}

/***************************************************************
 * get a unique event id
 * -------------------------------------------------------------
 * @param no parameter
 * -------------------------------------------------------------
 * @return: a unique event id
 **************************************************************/
uint32_t TimeWheel::createEventId(void)
{
    return m_increaseId++;
}

/***************************************************************************
 * caculate which slot this interval should belong to
 * -------------------------------------------------------------------------
 * @param[in] interval - interval of ms
 * @param[out] timePos - the slot position this interval should belong to
 * -------------------------------------------------------------------------
 * @return: no return
 **************************************************************************/
void TimeWheel::getTriggerTimeFromInterval(uint32_t interval, TimePos_t &timePos)
{
//get current time: ms
    uint32_t curTime = getCurrentMs(m_timePos);
// DEBUG_TIME_LINE("interval = %d,current ms = %d", interval, curTime);

//caculate which slot this interval should belong to
    uint32_t futureTime = curTime + interval;
// DEBUG_TIME_LINE("future ms = %d", futureTime);
    timePos.pos_min = (futureTime / 1000 / 60) % m_thirdLevelCount;
    timePos.pos_sec = (futureTime % (1000 * 60)) / 1000;
    timePos.pos_ms = (futureTime % 1000) / m_steps;

// DEBUG_TIME_LINE("next minPos=%d, secPos=%d, msPos=%d", timePos.pos_min, timePos.pos_sec, timePos.pos_ms);
}

/***************************************************************************
 * caculate current ms from TimePos
 * -------------------------------------------------------------------------
 * @param[in] timePos - the slot position
 * -------------------------------------------------------------------------
 * @return: current ms
 **************************************************************************/
uint32_t TimeWheel::getCurrentMs(TimePos_t timePos)
{
    return m_steps * timePos.pos_ms + timePos.pos_sec * 1000 + timePos.pos_min * 60 * 1000;
}

/***************************************************************************
 * process the events in this slot
 * -------------------------------------------------------------------------
 * @param[in] eventList - the event list in this slot
 * -------------------------------------------------------------------------
 * @return: 0 - success, other - fail
 **************************************************************************/
uint32_t TimeWheel::processEvent(std::list<Event_t> &eventList)
{
    // Snapshot base time position for consistent calculations
    TimePos_t basePos = m_timePos;
    uint32_t currentMs = getCurrentMs(basePos);

    // containers for reinsertion
    std::vector<Event_t> reinjectImmediate;

    // iterate through the provided list (these are popped from the main slots by caller)
    for (auto &event : eventList)
    {
        uint32_t lastProcessedMs = getCurrentMs(event.timePos);
        uint32_t distanceMs = (currentMs - lastProcessedMs + (m_secondLevelCount + 1) * 60 * 1000) % ((m_secondLevelCount + 1) * 60 * 1000);

        if (event.interval == distanceMs)
        {
            // this event is due
            if (m_concurrentDispatch.load())
            {
                // move event into task that will call cb and re-insert after execution
                Event_t evCopy = event;
                TimePos_t evBase = basePos; // capture base position
                enqueueTask([this, evCopy, evBase]() mutable {
                    if (evCopy.cb) evCopy.cb(evCopy.arg);
                    // reinsert the event after callback
                    std::lock_guard<std::mutex> lg(this->m_mutex);
                    evCopy.timePos = evBase;
                    this->insertEventToSlot(evCopy.interval, evCopy);
                });
            }
            else
            {
                // sequential: call directly then reinsert
                if (event.cb) event.cb(event.arg);
                reinjectImmediate.push_back(event);
                // set its start point to basePos
                reinjectImmediate.back().timePos = basePos;
            }
        }
        else
        {
            DEBUG_TIME_LINE("event->interval != distanceMs");
            // not due yet; reinsert with the remaining distance
            Event_t evCopy = event;
            evCopy.interval = distanceMs; // schedule after remaining distance
            reinjectImmediate.push_back(evCopy);
        }
    }

    // reinsert any events that must be reinjected immediately (either not-due ones, or sequentially processed ones)
    if (!reinjectImmediate.empty())
    {
        std::lock_guard<std::mutex> lg(m_mutex);
        for (auto &e : reinjectImmediate)
        {
            insertEventToSlot(e.interval, e);
        }
    }

    return 0;
}

/***************************************************************************
 * insert event to a slot
 * -------------------------------------------------------------------------
 * @param[in] interval - interval of ms
 * @param[in] event - the event to be inserted
 * -------------------------------------------------------------------------
 * @return: no return
 **************************************************************************/
void TimeWheel::insertEventToSlot(uint32_t interval, Event_t &event)
{
    DEBUG_TIME_LINE("insertEventToSlot");

    TimePos_t timePos = { 0 };

//caculate the which slot this event should be set to
    getTriggerTimeFromInterval(interval, timePos);
    {
        // DEBUG_TIME_LINE("timePos.pos_min=%d, m_timePos.pos_min=%d", timePos.pos_min, m_timePos.pos_min);
        // DEBUG_TIME_LINE("timePos.pos_sec=%d, m_timePos.pos_sec=%d", timePos.pos_sec, m_timePos.pos_sec);
        // DEBUG_TIME_LINE("timePos.pos_ms=%d, m_timePos.pos_ms=%d", timePos.pos_ms, m_timePos.pos_ms);

        // if minutes not equal to current minute, first insert it to it's minute slot
        if (timePos.pos_min != m_timePos.pos_min)
        {
            DEBUG_TIME_LINE("insert to %d minute", m_firstLevelCount + m_secondLevelCount + timePos.pos_min);
            m_eventSlotList[m_firstLevelCount + m_secondLevelCount + timePos.pos_min]
                    .push_back(event);
        }
        // if minutes is equal, but second changed, insert slot to this  integral point second
        else if (timePos.pos_sec != m_timePos.pos_sec)
        {
            DEBUG_TIME_LINE("insert to %d sec", m_firstLevelCount + timePos.pos_sec);
            m_eventSlotList[m_firstLevelCount + timePos.pos_sec].push_back(event);
        }
        //if minute and second is equal, mean this event will not be trigger in integral point, set it to ms slot
        else if (timePos.pos_ms != m_timePos.pos_ms)
        {
            DEBUG_TIME_LINE("insert to %d ms", timePos.pos_ms);
            m_eventSlotList[timePos.pos_ms].push_back(event);
        }
    }
}

void TimeWheel::setThreadPoolSize(size_t poolSize)
{
    m_desiredPoolSize.store(poolSize);
    // if pool is already running, restart with new size
    if (m_poolSize != 0 && poolSize != m_poolSize)
    {
        stopThreadPool();
        startThreadPool(poolSize == 0 ? std::max<size_t>(1, std::thread::hardware_concurrency()) : poolSize);
    }
}

void TimeWheel::run(void)
{
    while (!m_stopLoop.load())
    {
        // Wait for either the timeout (m_steps) or a stop notification
        std::unique_lock<std::mutex> lk(m_loopCvMutex);
        m_loopCv.wait_for(lk, std::chrono::milliseconds(m_steps), [this]() { return m_stopLoop.load(); });

        if (m_stopLoop.load()) break;

        TimePos pos = { 0 };
        m_lastTimePos = m_timePos;
        //update slot of current TimeWheel
        getTriggerTimeFromInterval(m_steps, pos);
        m_timePos = pos;

        std::list<Event_t> toProcess;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // if minute changed, process in integral point (minute)
            if (pos.pos_min != m_lastTimePos.pos_min)
            {
                size_t idx = m_timePos.pos_min + m_firstLevelCount + m_secondLevelCount;
                toProcess.swap(m_eventSlotList[idx]);
            }
            else if (pos.pos_sec != m_lastTimePos.pos_sec)
            {
                size_t idx = m_timePos.pos_sec + m_firstLevelCount;
                toProcess.swap(m_eventSlotList[idx]);
            }
            else if (pos.pos_ms != m_lastTimePos.pos_ms)
            {
                size_t idx = m_timePos.pos_ms;
                toProcess.swap(m_eventSlotList[idx]);
            }
        }

        if (!toProcess.empty())
            processEvent(toProcess);
    }
}

// ------------------ Thread pool implementation ------------------
void TimeWheel::startThreadPool(size_t workerCount)
{
    if (workerCount == 0) workerCount = 1;
    m_poolSize = workerCount;
    m_stopThreadPool.store(false);
    for (size_t i = 0; i < workerCount; ++i)
    {
        m_workers.emplace_back([this]() {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(this->m_taskMutex);
                    this->m_taskCv.wait(lk, [this]() { return this->m_stopThreadPool.load() || !this->m_taskQueue.empty(); });
                    if (this->m_stopThreadPool.load() && this->m_taskQueue.empty())
                        return;
                    task = std::move(this->m_taskQueue.front());
                    this->m_taskQueue.pop();
                }
                try {
                    if (task) task();
                } catch (...) {
                    DEBUG_TIME_LINE("exception in task");
                }
            }
        });
    }
}

void TimeWheel::stopThreadPool()
{
    m_stopThreadPool.store(true);
    m_taskCv.notify_all();
    for (auto &t : m_workers)
    {
        if (t.joinable()) t.join();
    }
    m_workers.clear();
    // clear remaining tasks
    std::lock_guard<std::mutex> lg(m_taskMutex);
    while (!m_taskQueue.empty()) m_taskQueue.pop();
}

void TimeWheel::enqueueTask(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lg(m_taskMutex);
        m_taskQueue.push(std::move(task));
    }
    m_taskCv.notify_one();
}

// runtime control for debug output
void setTimeWheelDebug(bool enable)
{
    g_enable_debug.store(enable);
}

// Ensure threadpool is stopped on unload: add destructor-like cleanup (could be improved)
// Note: add a static guard? For now, rely on process exit to clean threads, or call stopThreadPool explicitly.
TimeWheel::~TimeWheel()
{
    // signal the loop to stop
    m_stopLoop.store(true);

    // wake up the loop thread if it's waiting
    {
        std::lock_guard<std::mutex> lk(m_loopCvMutex);
        m_loopCv.notify_all();
    }

    // join the loop thread if it was created
    if (m_loopThread != 0)
    {
        pthread_join(m_loopThread, NULL);
        m_loopThread = 0;
    }

    // stop and join worker threads
    stopThreadPool();
}