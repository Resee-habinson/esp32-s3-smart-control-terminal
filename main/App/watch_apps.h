#ifndef WATCH_APPS_H_
#define WATCH_APPS_H_

#include <lvgl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "watch_storage.h"

/**
 * 手表全屏应用管理器。
 *
 * 菜单与小智界面由 WatchAppShell 持有，本类只拥有当前全屏应用覆盖层及其
 * LVGL 定时器。所有公开函数都必须在 LVGL 任务中，或持有 LVGL 锁时调用。
 */
class WatchApplications {
public:
    /**
     * 函    数：绑定手表主页 Screen
     * 参    数：watch_screen 手表主页 Screen，所有权仍归 LVGL
     * 返 回 值：true 参数有效；false 参数为空
     * 注意事项：只能初始化一次，且必须在 LVGL 初始化完成后调用
     */
    bool Initialize(lv_obj_t* watch_screen);

    /**
     * 函    数：打开指定菜单索引对应的全屏应用
     * 参    数：index 原手表源码中的 0～11 菜单索引
     * 返 回 值：true 已创建应用页面；false 索引无效或尚未初始化
     */
    bool Open(size_t index);

    /** 函数：从主页天气组件打开七日天气；参数：无；返回值：是否创建成功 */
    bool OpenWeather();

    /** 函数：关闭当前应用并返回主页；参数：无；返回值：无 */
    void Close();

    /** 函数：查询是否有应用正在显示；参数：无；返回值：true 表示应用覆盖层存在 */
    bool IsOpen() const { return overlay_ != nullptr; }

public:
    enum class AppId : uint8_t {
        kClock = 0,
        kPicture,
        kNovel,
        kVideo,
        kMusic,
        kGame,
        kCalculator,
        kStopwatch,
        kCalendar,
        kComic,
        kSettings,
        kFileManager,
        kWeather,
        kCount,
    };

    enum class StopwatchState : uint8_t { kStopped, kRunning, kPaused };
    enum class CountdownState : uint8_t { kStopped, kRunning, kPaused };
    enum class MediaMode : uint8_t { kNone, kList, kPicture, kVideo, kMusic, kComic };
    enum class MusicState : uint8_t { kIdle, kLoading, kPlaying, kPaused, kFinished, kError };
    enum class SettingsAction : uint8_t {
        kNone,
        kWifiConfig,
        kSaveWallpaper,
        kSetManualTime,
        kRescanSd,
        kSaveCarousel,
        kSaveWeather,
    };

private:
    enum class WeatherState : uint8_t { kIdle, kLoading, kReady, kError };

    struct WeatherDay {
        std::array<char, 6> date{};
        int16_t maximum_tenths = 0;
        int16_t minimum_tenths = 0;
        int16_t code = 0;
    };

    struct StopwatchUi {
        lv_obj_t* time_label = nullptr;
        lv_obj_t* start_button = nullptr;
        lv_obj_t* pause_button = nullptr;
        lv_obj_t* reset_button = nullptr;
    };

    struct CountdownUi {
        lv_obj_t* minute_roller = nullptr;
        lv_obj_t* second_roller = nullptr;
        lv_obj_t* colon_label = nullptr;
        lv_obj_t* time_label = nullptr;
        lv_obj_t* arc = nullptr;
        lv_obj_t* start_button = nullptr;
        lv_obj_t* pause_button = nullptr;
        lv_obj_t* reset_button = nullptr;
    };

    void CreateClock();
    void CreatePicture();
    void CreateNovel();
    void CreateVideo();
    void CreateMusic();
    void CreateGame();
    void CreateCalculator();
    void CreateStopwatch();
    void CreateCalendar();
    void CreateComic();
    void CreateSettings();
    void ShowSettingsDetail(const char* item_text);
    void CreateWeather();
    void CreateFileManager();
    void RefreshNovelList();
    void OpenNovel(size_t index);
    void ShowNovelPage(size_t offset, bool remember_current);
    void ReturnToNovelList();
    void RefreshFileManager();
    void OpenFileManagerEntry(size_t index);
    void ShowFileDetails(size_t index);
    void CloseFileDetails();
    void CreateMediaList(AppId id);
    void OpenMediaEntry(size_t index);
    void ShowPicture(size_t index);
    void ShowVideo(size_t index);
    void ShowMusic(size_t index);
    void ShowComic(size_t index);
    void ReturnToMediaList();
    void CleanupMedia();
    void UpdateMedia();
    void UpdatePicture();
    void UpdateVideo();
    void UpdateMusic();
    void UpdateComic();
    bool ReadNextJpegFrame(FILE* file, std::vector<uint8_t>* frame, size_t maximum_size);
    bool LoadComicFrame(uint32_t frame_index);
    void StartMusicTask();
    void StopMusicTask();
    static void MusicTaskEntry(void* parameter);
    void UpdateActiveApplication();
    void UpdateClock();
    void UpdateStopwatch();
    void UpdateCountdown();
    void RefreshGame();
    void MoveGame(int direction);
    void SpawnGameTile();
    void HandleCalculatorToken(const char* token);
    void EvaluateCalculator();
    void ShiftCalendarMonth(int delta);
    void RefreshCalendar();
    void SetStopwatchButtons();
    void SetCountdownButtons();
    void StartWeatherFetch();
    void UpdateWeather();
    void RenderWeather();
    static void WeatherTaskEntry(void* parameter);
    static void WeatherRefreshCallback(lv_event_t* event);

    static void AppTimerCallback(lv_timer_t* timer);
    static void GameTouchCallback(lv_event_t* event);
    static void CalculatorButtonCallback(lv_event_t* event);
    static void NovelItemCallback(lv_event_t* event);
    static void NovelPreviousCallback(lv_event_t* event);
    static void NovelNextCallback(lv_event_t* event);
    static void NovelListCallback(lv_event_t* event);
    static void StopwatchStartCallback(lv_event_t* event);
    static void StopwatchPauseCallback(lv_event_t* event);
    static void StopwatchResetCallback(lv_event_t* event);
    static void CountdownStartCallback(lv_event_t* event);
    static void CountdownPauseCallback(lv_event_t* event);
    static void CountdownResetCallback(lv_event_t* event);
    static void CalendarPreviousCallback(lv_event_t* event);
    static void CalendarNextCallback(lv_event_t* event);
    static void SettingsItemCallback(lv_event_t* event);
    static void SettingsBackCallback(lv_event_t* event);
    static void SettingsActionCallback(lv_event_t* event);
    static void SettingsAdjustCallback(lv_event_t* event);
    static void FileManagerItemCallback(lv_event_t* event);
    static void FileManagerBackCallback(lv_event_t* event);
    static void FileManagerTopCallback(lv_event_t* event);
    static void FileManagerNextCallback(lv_event_t* event);
    static void FileDetailsCloseCallback(lv_event_t* event);
    static void FileDetailsDeleteCallback(lv_event_t* event);
    static void MediaItemCallback(lv_event_t* event);
    static void MediaBackCallback(lv_event_t* event);
    static void MediaPreviousCallback(lv_event_t* event);
    static void MediaNextCallback(lv_event_t* event);
    static void PictureCarouselCallback(lv_event_t* event);
    static void VideoPlayCallback(lv_event_t* event);
    static void MusicPlayCallback(lv_event_t* event);
    static void MusicVolumeCallback(lv_event_t* event);

    lv_obj_t* watch_screen_ = nullptr;  // 仅引用手表 Screen，不负责销毁
    lv_obj_t* overlay_ = nullptr;       // 当前应用覆盖层，由本类创建和删除
    lv_timer_t* app_timer_ = nullptr;   // 当前应用 UI 更新定时器，仅在应用打开时存在
    AppId active_app_ = AppId::kCount;

    lv_obj_t* clock_wallpaper_ = nullptr;
    lv_obj_t* clock_time_label_ = nullptr;
    lv_obj_t* clock_second_label_ = nullptr;
    lv_obj_t* clock_date_label_ = nullptr;
    std::string clock_wallpaper_lvgl_path_;  // LVGL 持有路径指针期间由应用实例保存字符串

    std::vector<WatchStorage::Entry> novel_entries_;
    std::string novel_path_;
    std::vector<size_t> novel_page_history_;
    lv_obj_t* novel_list_ = nullptr;
    lv_obj_t* novel_content_label_ = nullptr;
    lv_obj_t* novel_progress_label_ = nullptr;
    lv_obj_t* novel_previous_button_ = nullptr;
    lv_obj_t* novel_next_button_ = nullptr;
    size_t novel_page_start_ = 0;
    size_t novel_next_offset_ = 0;
    size_t novel_file_size_ = 0;

    std::array<std::array<uint8_t, 4>, 4> game_grid_{};
    std::array<std::array<lv_obj_t*, 4>, 4> game_tiles_{};
    lv_obj_t* game_score_label_ = nullptr;
    uint32_t game_score_ = 0;
    lv_point_t game_press_point_{};

    lv_obj_t* calculator_textarea_ = nullptr;
    std::string calculator_expression_;

    StopwatchUi stopwatch_ui_{};
    StopwatchState stopwatch_state_ = StopwatchState::kStopped;
    int64_t stopwatch_started_us_ = 0;
    int64_t stopwatch_elapsed_us_ = 0;

    CountdownUi countdown_ui_{};
    CountdownState countdown_state_ = CountdownState::kStopped;
    int64_t countdown_deadline_us_ = 0;
    int64_t countdown_remaining_us_ = 0;
    int64_t countdown_total_us_ = 0;

    lv_obj_t* calendar_title_label_ = nullptr;
    lv_obj_t* calendar_days_container_ = nullptr;
    int calendar_year_ = 2026;
    int calendar_month_ = 1;

    lv_obj_t* settings_list_ = nullptr;
    lv_obj_t* settings_detail_ = nullptr;
    lv_obj_t* settings_status_label_ = nullptr;
    lv_obj_t* settings_selector_ = nullptr;
    lv_obj_t* settings_input_a_ = nullptr;
    lv_obj_t* settings_input_b_ = nullptr;
    std::array<lv_obj_t*, 5> settings_time_inputs_{};
    std::vector<WatchStorage::Entry> settings_wallpapers_;
    SettingsAction settings_action_ = SettingsAction::kNone;

    std::string file_manager_path_ = "/";
    std::vector<WatchStorage::Entry> file_manager_entries_;
    lv_obj_t* file_manager_title_ = nullptr;
    lv_obj_t* file_manager_list_ = nullptr;
    lv_obj_t* file_details_dialog_ = nullptr;
    lv_obj_t* file_delete_label_ = nullptr;
    size_t file_details_index_ = 0;
    size_t file_manager_page_ = 0;
    bool file_delete_armed_ = false;

    MediaMode media_mode_ = MediaMode::kNone;
    std::string media_root_;
    std::string media_path_;
    std::string media_lvgl_path_;
    std::vector<WatchStorage::Entry> media_entries_;
    size_t media_index_ = 0;
    lv_obj_t* media_list_ = nullptr;
    lv_obj_t* media_image_ = nullptr;
    lv_obj_t* media_title_ = nullptr;
    lv_obj_t* media_status_ = nullptr;
    lv_obj_t* media_play_button_label_ = nullptr;
    lv_obj_t* media_progress_ = nullptr;
    lv_obj_t* media_volume_ = nullptr;
    bool picture_carousel_ = false;
    int64_t picture_next_us_ = 0;

    FILE* video_file_ = nullptr;
    size_t video_file_size_ = 0;
    bool video_playing_ = false;
    std::vector<uint8_t> encoded_frame_;
    lv_image_dsc_t encoded_image_{};

    FILE* comic_file_ = nullptr;
    std::vector<uint64_t> comic_offsets_;
    uint32_t comic_frame_ = 0;

    std::string music_task_path_;
    std::atomic<TaskHandle_t> music_task_{nullptr};
    std::atomic_bool music_stop_{false};
    std::atomic_bool music_paused_{false};
    std::atomic<MusicState> music_state_{MusicState::kIdle};
    std::atomic<uint32_t> music_progress_permille_{0};
    std::atomic<uint32_t> music_elapsed_seconds_{0};
    std::atomic<uint8_t> music_volume_percent_{70};
    bool music_audio_owned_ = false;

    std::array<WeatherDay, 7> weather_days_{};
    std::atomic<TaskHandle_t> weather_task_{nullptr};
    std::atomic<WeatherState> weather_state_{WeatherState::kIdle};
    std::atomic_bool weather_cancel_{false};
    WeatherState weather_rendered_state_ = WeatherState::kIdle;
    size_t weather_day_count_ = 0;
    lv_obj_t* weather_panel_ = nullptr;
    lv_obj_t* weather_status_ = nullptr;
};

#endif  // WATCH_APPS_H_
