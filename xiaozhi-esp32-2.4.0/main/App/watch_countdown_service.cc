#include "watch_countdown_service.h"

#include <esp_timer.h>

WatchCountdownService& WatchCountdownService::Instance() {
    static WatchCountdownService service;
    return service;
}

bool WatchCountdownService::Start(int64_t duration_us) {
    if (duration_us <= 0) return false;
    total_us_ = duration_us;
    remaining_us_ = duration_us;
    deadline_us_ = esp_timer_get_time() + duration_us;
    finished_event_ = false;
    state_ = State::kRunning;
    return true;
}

WatchCountdownService::State WatchCountdownService::TogglePause() {
    const int64_t now_us = esp_timer_get_time();
    Refresh(now_us);
    if (state_ == State::kRunning) {
        remaining_us_ = deadline_us_ - now_us;
        state_ = State::kPaused;
    } else if (state_ == State::kPaused) {
        deadline_us_ = now_us + remaining_us_;
        state_ = State::kRunning;
    }
    return state_;
}

void WatchCountdownService::Reset() {
    state_ = State::kStopped;
    deadline_us_ = 0;
    remaining_us_ = 0;
    total_us_ = 0;
    finished_event_ = false;
}

WatchCountdownService::Snapshot WatchCountdownService::GetSnapshot() {
    const int64_t now_us = esp_timer_get_time();
    Refresh(now_us);
    int64_t remaining_us = remaining_us_;
    if (state_ == State::kRunning) remaining_us = deadline_us_ - now_us;
    return {state_, remaining_us > 0 ? remaining_us : 0, total_us_};
}

bool WatchCountdownService::TakeFinishedEvent() {
    Refresh(esp_timer_get_time());
    const bool pending = finished_event_;
    finished_event_ = false;
    return pending;
}

void WatchCountdownService::Refresh(int64_t now_us) {
    if (state_ != State::kRunning || now_us < deadline_us_) return;
    state_ = State::kStopped;
    deadline_us_ = 0;
    remaining_us_ = 0;
    total_us_ = 0;
    finished_event_ = true;
}
