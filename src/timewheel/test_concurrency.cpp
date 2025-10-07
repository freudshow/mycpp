#include "timeWheel.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

static std::atomic<int> firedCount { 0 };
static std::mutex firedMutex;
static std::condition_variable firedCv;

struct TestArg {
        int id;
};

void callback(void *arg)
{
    TestArg *t = (TestArg*) arg;
    // simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    firedCount.fetch_add(1, std::memory_order_relaxed);
    firedCv.notify_one();
}

int concurrenceTest()
{
    const int N = 20; // number of simultaneous events
    const int steps = 100; // ms

    {
        TimeWheel wheel(steps, 1, true); // run in background
        wheel.setConcurrentDispatch(true);

        vector<TestArg> args(N);
        for (int i = 0; i < N; ++i)
        {
            args[i].id = i;
            wheel.createTimingEvent(steps, callback, &args[i]);
        }

        // wait for at least N firings (first round)
        {
            std::unique_lock<std::mutex> lk(firedMutex);
            bool ok = firedCv.wait_for(lk, std::chrono::seconds(2), [&]()
                    {   return firedCount.load() >= N;});
            if (!ok)
            {
                cout << "Test FAILED: expected at least " << N << " firings within 2s, got " << firedCount.load() << "\n";
                return 1;
            }
        }
        cout << "First round observed: " << firedCount.load() << " firings\n";

        // wait for second round (each recurring event should be reinserted and fire again)
        {
            std::unique_lock<std::mutex> lk(firedMutex);
            bool ok = firedCv.wait_for(lk, std::chrono::seconds(3), [&]()
                    {   return firedCount.load() >= 2 * N;});
            if (!ok)
            {
                cout << "Test FAILED: expected at least " << 2 * N << " firings within 3s, got " << firedCount.load() << "\n";
                return 1;
            }
        }
        cout << "Second round observed: " << firedCount.load() - N << " additional firings (total " << firedCount.load() << ")\n";

        // stop wheel by letting destructor run at scope exit
    }

    cout << "Test PASSED\n";
    return 0;
}
