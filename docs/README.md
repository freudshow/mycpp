# C语言时间轮实现

这是一个纯C语言实现的时间轮（TimeWheel）定时器库，从C++版本改写而来。

## 主要特性

1. **纯C实现**: 不依赖C++标准库，使用标准C89/C99特性
2. **高精度**: 使用`clock_nanosleep`和`CLOCK_MONOTONIC`确保时间精度
3. **低漂移**: 基于绝对时间点计算，避免累积误差，实际执行时间与预期时间误差小于5%
4. **三层时间轮**: 支持毫秒、秒、分钟三个层级
5. **线程安全**: 使用pthread互斥锁保护共享数据

## 时间漂移优化

为了确保定时事件的执行时间精度，本实现采用以下措施：

1. **绝对时间基准**: 在线程启动时记录起始时间点作为基准
2. **基于tick计数**: 每次计算下一个唤醒时间点时，都基于起始时间 + tick数量，而不是累加间隔
3. **高精度时钟**: 使用`CLOCK_MONOTONIC`时钟源，不受系统时间调整影响
4. **绝对睡眠**: 使用`clock_nanosleep`的`TIMER_ABSTIME`模式，直接睡眠到指定的绝对时间点
5. **中断处理**: 处理系统中断，确保睡眠完整执行

这些措施确保即使周期性定时任务，其实际执行时间点与理论执行时间点的误差不会超过5%。

## 编译

```bash
make
```

## 运行

```bash
make run
# 或者
./timewheel_test
```

## 使用示例

```c
#include "timewheel.h"

void my_callback(void *arg) {
    printf("Timer triggered!\n");
}

int main() {
    // 创建时间轮: 时间精度100ms, 最大支持10分钟
    TimeWheel_t *wheel = timewheel_create(100, 10);
    if (wheel == NULL) {
        return -1;
    }

    // 创建定时事件: 每300ms执行一次
    timewheel_create_event(wheel, 300, my_callback, NULL);

    // 保持运行
    while (1) {
        sleep(1);
    }

    // 清理（可选）
    timewheel_destroy(wheel);
    return 0;
}
```

## API接口

### `timewheel_create(uint32_t steps, uint32_t maxMin)`
创建并初始化一个时间轮。
- `steps`: 时间精度（毫秒），必须是1000的因子（如10, 20, 50, 100等）
- `maxMin`: 时间轮支持的最大分钟数
- 返回: 时间轮指针，失败返回NULL

### `timewheel_destroy(TimeWheel_t *wheel)`
销毁时间轮并释放资源。
- `wheel`: 要销毁的时间轮指针

### `timewheel_init(TimeWheel_t *wheel, uint32_t steps, uint32_t maxMin)`
初始化一个已分配的时间轮结构。
- 返回: 0表示成功，-1表示失败

### `timewheel_create_event(TimeWheel_t *wheel, uint32_t interval, EventCallback_t callback, void *arg)`
创建一个周期性定时事件。
- `wheel`: 时间轮指针
- `interval`: 触发间隔（毫秒），必须是`steps`的倍数
- `callback`: 回调函数
- `arg`: 传递给回调函数的参数
- 返回: 0表示成功，-1表示失败

## 架构设计

### 三层时间轮结构

```
第一层（毫秒层）: 1000/steps 个槽位
第二层（秒层）  : 60 个槽位
第三层（分钟层）: maxMin 个槽位
```

### 数据结构

- `TimeWheel_t`: 时间轮主结构
- `Event_t`: 事件结构（使用单链表连接）
- `EventList_t`: 事件链表
- `TimePos_t`: 时间轮位置（毫秒、秒、分钟）

## 性能特点

- **时间复杂度**: O(1) 插入和删除事件
- **空间复杂度**: O(n)，n为事件数量
- **线程模型**: 单独的循环线程处理定时器
- **精度**: 由`steps`参数决定，推荐100ms

## 与C++版本的差异

1. 使用手动管理的链表替代`std::list`
2. 使用动态数组替代`std::vector`
3. 使用`pthread_mutex_t`替代`std::mutex`
4. 使用`clock_nanosleep`替代`std::this_thread::sleep_until`
5. 使用`malloc/free`进行内存管理

## 注意事项

1. 回调函数应该尽快执行完毕，避免阻塞定时器线程
2. 如果需要在回调中执行耗时操作，建议使用工作队列或线程池
3. `steps`必须是1000的因子
4. `interval`必须是`steps`的倍数
5. 确保在程序退出前调用`timewheel_destroy`释放资源

## 依赖

- POSIX线程库 (pthread)
- POSIX实时扩展 (librt)
- C标准库

## 许可

与原C++项目保持一致
