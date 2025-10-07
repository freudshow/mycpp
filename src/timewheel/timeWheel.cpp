#include "timeWheel.h"
#include <sys/time.h>
#include <iostream>
#include <memory.h>
#include <chrono>
#include <thread>
#include <stdarg.h>

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
}

TimeWheel::TimeWheel(uint32_t steps, uint32_t maxMin)
{
    m_steps = 0;
    m_firstLevelCount = 0;
    m_secondLevelCount = 60;
    m_thirdLevelCount = 0;
    m_increaseId = 0;
    m_loopThread = 0;
    memset(&m_timePos, 0, sizeof(m_timePos));

    initTimeWheel(steps, maxMin);
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
    while (1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeWheel->m_steps)); //毫秒针走一格，推进时间轮
        // DEBUG_TIME_LINE("wake up");
        TimePos pos = { 0 };
        TimePos m_lastTimePos = timeWheel->m_timePos;        //记住上一次推进时的时针位置
        //update slot of current TimeWheel
        timeWheel->getTriggerTimeFromInterval(timeWheel->m_steps, pos);        //获取当前的时针位置
        timeWheel->m_timePos = pos;
        {
            std::unique_lock<std::mutex> lock(timeWheel->m_mutex);
            // if minute changed, process in integral point (minute)
            if (pos.pos_min != m_lastTimePos.pos_min)
            {
                // DEBUG_TIME_LINE("minutes changed");
                std::list<Event_t> *eventList = &timeWheel->m_eventSlotList[timeWheel->m_timePos.pos_min + timeWheel->m_firstLevelCount + timeWheel->m_secondLevelCount];
                timeWheel->processEvent(*eventList);
                eventList->clear();
            }
            else if (pos.pos_sec != m_lastTimePos.pos_sec)
            {
                //in same minute, but second changed, now is in this integral second
                // DEBUG_TIME_LINE("second changed");
                std::list<Event_t> *eventList = &timeWheel->m_eventSlotList[timeWheel->m_timePos.pos_sec + timeWheel->m_firstLevelCount];
                timeWheel->processEvent(*eventList);
                eventList->clear();
            }
            else if (pos.pos_ms != m_lastTimePos.pos_ms)
            {
                //now in this ms
                // DEBUG_TIME_LINE("ms changed");
                std::list<Event_t> *eventList = &timeWheel->m_eventSlotList[timeWheel->m_timePos.pos_ms];
                timeWheel->processEvent(*eventList);
                eventList->clear();
            }
            // DEBUG_TIME_LINE("loop over");
        }
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
void TimeWheel::initTimeWheel(uint32_t steps, uint32_t maxMin)
{
    if (1000 % steps != 0)
    {
        DEBUG_TIME_LINE("invalid steps");
        return;
    }

    m_steps = steps;
    m_firstLevelCount = 1000 / steps;
    m_thirdLevelCount = maxMin;

    m_eventSlotList.resize(m_firstLevelCount + m_secondLevelCount + m_thirdLevelCount);
    uint32_t ret = pthread_create(&m_loopThread, NULL, loopForInterval, this);
    if (ret != 0)
    {
        DEBUG_TIME_LINE("create thread error:%s", strerror(errno));
        return;
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
// DEBUG_TIME_LINE("eventList.size=%d", eventList.size());

//process the event for current slot
    for (auto event = eventList.begin(); event != eventList.end(); event++)
    {
        //caculate the current ms
        uint32_t currentMs = getCurrentMs(m_timePos);                //当前时间轮对应的毫秒数
        DEBUG_TIME_LINE("currentMs=%d", currentMs);
        //caculate last  time(ms) this event was processed
        uint32_t lastProcessedMs = getCurrentMs(event->timePos);                //上一次事件所在的槽位
        //caculate the distance between now and last time(ms)
        uint32_t distanceMs = (currentMs - lastProcessedMs + (m_secondLevelCount + 1) * 60 * 1000) % ((m_secondLevelCount + 1) * 60 * 1000);

        //if interval == distanceMs, need process this event
        if (event->interval == distanceMs)
        {
            //process event
            event->cb(event->arg);
            //get now pos as this event's start point
            event->timePos = m_timePos;
            //add this event to slot
            insertEventToSlot(event->interval, *event);
        }
        else
        {
            //this condition will be trigger when process the integral point
            DEBUG_TIME_LINE("event->interval != distanceMs");
            // although this event in this positon, but it not arriving timing, it will continue move to next slot caculate by distance ms.
            insertEventToSlot(distanceMs, *event);
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
