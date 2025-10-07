#include "timeWheel.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace std;

static std::atomic<int> *g_fired_ptr = nullptr;
static std::condition_variable *g_cv_ptr = nullptr;
static std::mutex *g_mtx_ptr = nullptr;

void stress_cb(void *a)
{
    (void) a;
    if (g_fired_ptr)
    {
        g_fired_ptr->fetch_add(1, std::memory_order_relaxed);
    }
    if (g_cv_ptr)
    {
        g_cv_ptr->notify_one();
    }
}

int main(int argc, char **argv)
{
    // ensure debug output is disabled for clean metrics
    setTimeWheelDebug(false);

    const int N = 10000; // many thousands
    const int steps = 10; // ms
    std::atomic<int> fired { 0 };
    std::mutex mtx;
    std::condition_variable cv;

    vector<int> ids(N);
    for (int i = 0; i < N; ++i)
        ids[i] = i;

    // set global pointers used by the C-style callback
    g_fired_ptr = &fired;
    g_cv_ptr = &cv;
    g_mtx_ptr = &mtx;

    // run wheel in background with a pool sized to hardware concurrency
    TimeWheel wheel(steps, 1, true);
    size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
    wheel.setThreadPoolSize(hw);
    wheel.setConcurrentDispatch(true);

    auto t0 = chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
    {
        wheel.createTimingEvent(steps, stress_cb, &ids[i]);
    }

    // wait up to 15 seconds for all to fire
    auto deadline = chrono::steady_clock::now() + chrono::seconds(15);
    {
        unique_lock<mutex> lk(mtx);
        while (fired.load() < N)
        {
            if (cv.wait_until(lk, deadline) == cv_status::timeout)
                break;
        }
    }
    auto t1 = chrono::steady_clock::now();
    int observed = fired.load();
    auto ms = chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();

    cout << "Stress test: scheduled=" << N << " observed=" << observed << " elapsed_ms=" << ms << "\n";
    if (observed < N)
    {
        cout << "Test FAILED: only " << observed << " of " << N << " fired in time.\n";
        return 1;
    }

    cout << "Test PASSED\n";
    return 0;
}
