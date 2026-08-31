#ifndef WATCH_APP_SHELL_H_
#define WATCH_APP_SHELL_H_

#include <lvgl.h>

#include "watch_apps.h"

/**
 * 手表应用外壳。
 *
 * 原小智界面和手表界面分别使用独立的 LVGL Screen。切换时只改变当前
 * Screen，不删除任何对象，从而允许小智协议和语音状态在后台继续更新。
 */
class WatchAppShell {
public:
    /**
     * 函    数：初始化手表应用外壳
     * 参    数：xiaozhi_screen 小智原始界面所在的 Screen，所有权仍归 LVGL
     * 返 回 值：true 初始化成功；false 表示参数无效或 Screen 创建失败
     * 注意事项：必须在 LVGL 初始化且小智 SetupUI 完成后调用
     */
    bool Initialize(lv_obj_t* xiaozhi_screen);

    /** 函数：显示原始小智应用；参数：无；返回值：无 */
    void ShowXiaozhi();

    /** 函数：显示手表主界面；参数：无；返回值：无 */
    void ShowWatch();

    /** 函数：在手表与小智应用之间切换；参数：无；返回值：无 */
    void Toggle();

    /** 函数：应用内短按 BOOT 返回主页；参数：无；返回值：true 表示已处理 */
    bool HandleBootClick();

    /** 函数：查询当前是否显示小智；参数：无；返回值：true 表示小智可见 */
    bool IsXiaozhiVisible() const { return active_app_ == ActiveApp::kXiaozhi; }

private:
    enum class ActiveApp { kWatch, kXiaozhi };

    static void MenuClickCallback(lv_event_t* event);
    static void WeatherClickCallback(lv_event_t* event);
    static void MenuScrollCallback(lv_event_t* event);
    static void LayoutMenu(lv_obj_t* menu);
    void CreateWatchScreen();
    void CreateClock(lv_obj_t* parent);
    void CreateWeatherWidget(lv_obj_t* parent);
    void CreateMenu(lv_obj_t* parent);
    void UpdateClock();
    static void ClockTimerCallback(lv_timer_t* timer);

    lv_obj_t* xiaozhi_screen_ = nullptr;  // 小智原界面，外壳只引用、不负责销毁
    lv_obj_t* watch_screen_ = nullptr;    // 手表主界面，由 LVGL 管理生命周期
    lv_obj_t* hour_label_ = nullptr;
    lv_obj_t* minute_label_ = nullptr;
    lv_obj_t* second_label_ = nullptr;
    lv_obj_t* weather_image_ = nullptr;  // 主页天气图标，由 LVGL 管理生命周期
    lv_timer_t* clock_timer_ = nullptr;
    WatchApplications applications_;
    ActiveApp active_app_ = ActiveApp::kWatch;
};

#endif  // WATCH_APP_SHELL_H_
