#ifndef WATCH_COUNTDOWN_SERVICE_H_
#define WATCH_COUNTDOWN_SERVICE_H_

#include <cstdint>

/**
 * 手表倒计时后台服务。
 *
 * 服务只保存单调时钟截止点，不持有任何 LVGL 对象，因此离开计时页面或切换到
 * 小智界面后仍可继续计时。所有公开接口由 LVGL 任务调用，无需跨任务锁。
 */
class WatchCountdownService {
public:
    enum class State : uint8_t { kStopped, kRunning, kPaused };

    struct Snapshot {
        State state = State::kStopped;
        int64_t remaining_us = 0;
        int64_t total_us = 0;
    };

    /** 函数：取得全局倒计时服务；参数：无；返回值：服务单例引用 */
    static WatchCountdownService& Instance();

    /** 函数：启动新倒计时；参数：duration_us 总时长，单位微秒且必须大于零；返回值：是否启动成功 */
    bool Start(int64_t duration_us);

    /** 函数：切换运行/暂停状态；参数：无；返回值：切换后的状态 */
    State TogglePause();

    /** 函数：停止并清空倒计时；参数：无；返回值：无 */
    void Reset();

    /** 函数：读取当前状态快照；参数：无；返回值：剩余时间、总时间和状态 */
    Snapshot GetSnapshot();

    /** 函数：消费一次倒计时完成事件；参数：无；返回值：true 表示本次取得了新完成事件 */
    bool TakeFinishedEvent();

private:
    WatchCountdownService() = default;
    void Refresh(int64_t now_us);

    State state_ = State::kStopped;
    int64_t deadline_us_ = 0;
    int64_t remaining_us_ = 0;
    int64_t total_us_ = 0;
    bool finished_event_ = false;
};

#endif  // WATCH_COUNTDOWN_SERVICE_H_
