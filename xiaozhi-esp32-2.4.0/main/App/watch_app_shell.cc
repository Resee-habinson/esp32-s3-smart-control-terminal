#include "watch_app_shell.h"

#include "watch_menu_assets.h"
#include "watch_weather_assets.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <ctime>

namespace {
constexpr char kTag[] = "watch_app";
constexpr int kDisplayWidth = 480;
constexpr int kDisplayHeight = 320;
constexpr int kClockPanelWidth = 130;
constexpr int kMenuWidth = kDisplayWidth - kClockPanelWidth - 12;
constexpr int kMenuHeight = 128;
constexpr int kMenuIconSize = 60;

lv_image_dsc_t s_menu_images[WATCH_MENU_ICON_COUNT];
lv_image_dsc_t s_weather_images[WEATHER_FRAME_CNT];
bool s_images_initialized = false;

/** 函数：生成兼容 LVGL 9 的图像描述符；参数：无；返回值：无 */
void InitializeImageDescriptors() {
    if (s_images_initialized) {
        return;
    }
    for (size_t i = 0; i < WATCH_MENU_ICON_COUNT; ++i) {
        s_menu_images[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_menu_images[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_menu_images[i].header.flags = 0;
        s_menu_images[i].header.w = WATCH_MENU_ICON_WIDTH;
        s_menu_images[i].header.h = WATCH_MENU_ICON_HEIGHT;
        s_menu_images[i].header.stride = WATCH_MENU_ICON_WIDTH * sizeof(uint16_t);
        s_menu_images[i].data_size = WATCH_MENU_ICON_WIDTH * WATCH_MENU_ICON_HEIGHT * sizeof(uint16_t);
        s_menu_images[i].data = reinterpret_cast<const uint8_t*>(watch_menu_pixels[i]);
    }
    /* 将原 Arduino PROGMEM 天气图片包装为 LVGL 9 RGB565 描述符。 */
    for (size_t i = 0; i < WEATHER_FRAME_CNT; ++i) {
        s_weather_images[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_weather_images[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_weather_images[i].header.flags = 0;
        s_weather_images[i].header.w = WEATHER_IMG_WIDTH;
        s_weather_images[i].header.h = WEATHER_IMG_HEIGHT;
        s_weather_images[i].header.stride = WEATHER_IMG_WIDTH * sizeof(uint16_t);
        s_weather_images[i].data_size = WEATHER_IMG_WIDTH * WEATHER_IMG_HEIGHT * sizeof(uint16_t);
        s_weather_images[i].data = reinterpret_cast<const uint8_t*>(watch_weather_pixels[i]);
    }
    s_images_initialized = true;
}
}  // namespace

bool WatchAppShell::Initialize(lv_obj_t* xiaozhi_screen) {
    if (xiaozhi_screen == nullptr || watch_screen_ != nullptr) {
        ESP_LOGE(kTag, "Invalid shell initialization state");
        return false;
    }

    xiaozhi_screen_ = xiaozhi_screen;
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(kTag, "LVGL lock timeout while creating watch UI");
        return false;
    }
    CreateWatchScreen();
    const bool created = watch_screen_ != nullptr;
    if (created) {
        lv_screen_load(watch_screen_);
        active_app_ = ActiveApp::kWatch;
    }
    lvgl_port_unlock();
    return created;
}

void WatchAppShell::CreateWatchScreen() {
    InitializeImageDescriptors();
    watch_screen_ = lv_obj_create(nullptr);
    if (watch_screen_ == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(watch_screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(watch_screen_, LV_OPA_COVER, 0);

    /* 使用完整 480×320 逻辑画布，消除旧版 240×280 居中造成的左右黑框。 */
    lv_obj_t* canvas = lv_obj_create(watch_screen_);
    lv_obj_set_size(canvas, kDisplayWidth, kDisplayHeight);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_style_bg_color(canvas, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_style_pad_all(canvas, 0, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    CreateClock(canvas);
    CreateWeatherWidget(canvas);
    CreateMenu(canvas);
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);
    UpdateClock();
}

void WatchAppShell::CreateClock(lv_obj_t* parent) {
    hour_label_ = lv_label_create(parent);
    minute_label_ = lv_label_create(parent);
    second_label_ = lv_label_create(parent);

    for (lv_obj_t* label : {hour_label_, minute_label_, second_label_}) {
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }

    /* 使用随当前 LVGL 一同编译的字体，避免旧版字体描述符触发空回调重启。 */
    lv_obj_set_style_text_font(hour_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_font(minute_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_font(second_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(hour_label_, LV_ALIGN_TOP_LEFT, 24, 24);
    lv_obj_align_to(minute_label_, hour_label_, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_align_to(second_label_, minute_label_, LV_ALIGN_OUT_BOTTOM_LEFT, 4, 8);
}

/**
 * 函    数：创建主页天气图标
 * 参    数：parent 主页画布，所有权由 LVGL 管理
 * 返 回 值：无
 * 注意事项：当前仅恢复原工程天气资源与入口图标；天气数据接入后可按 WMO 代码切换描述符。
 */
void WatchAppShell::CreateWeatherWidget(lv_obj_t* parent) {
    constexpr size_t kDefaultWeatherIconIndex = 9;  // 原资源第 9 帧为“多云”，用于无天气数据时的入口图标

    weather_image_ = lv_image_create(parent);
    lv_image_set_src(weather_image_, &s_weather_images[kDefaultWeatherIconIndex]);
    lv_obj_set_size(weather_image_, WEATHER_IMG_WIDTH, WEATHER_IMG_HEIGHT);
    lv_obj_set_pos(weather_image_, 15, 190);
    lv_obj_remove_flag(weather_image_, LV_OBJ_FLAG_SCROLLABLE);
}

void WatchAppShell::CreateMenu(lv_obj_t* parent) {
    lv_obj_t* menu = lv_obj_create(parent);
    lv_obj_set_size(menu, kMenuWidth, kMenuHeight);
    lv_obj_align(menu, LV_ALIGN_RIGHT_MID, -6, 44);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu, 0, 0);
    lv_obj_set_style_pad_all(menu, 0, 0);
    lv_obj_set_scroll_dir(menu, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(menu, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(menu, MenuScrollCallback, LV_EVENT_SCROLL, nullptr);

    for (size_t i = 0; i < WATCH_MENU_ICON_COUNT; ++i) {
        lv_obj_t* image = lv_image_create(menu);
        lv_image_set_src(image, &s_menu_images[i]);
        lv_obj_set_size(image, kMenuIconSize, kMenuIconSize);
        lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(image, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(image, MenuClickCallback, LV_EVENT_CLICKED, this);
    }

    lv_obj_update_layout(menu);
    lv_obj_scroll_to_view(lv_obj_get_child(menu, 0), LV_ANIM_OFF);
    LayoutMenu(menu);
}

/**
 * 函    数：按图标与菜单中心的距离计算弧形位移和缩放
 * 参    数：menu 横向滚动菜单对象
 * 返 回 值：无
 * 注意事项：仅在 LVGL 任务或持有 LVGL 锁时调用
 */
void WatchAppShell::LayoutMenu(lv_obj_t* menu) {
    lv_area_t menu_area = {};
    lv_obj_get_coords(menu, &menu_area);
    const int32_t menu_center_x = (menu_area.x1 + menu_area.x2) / 2;
    const int32_t half_width = lv_area_get_width(&menu_area) / 2;
    const uint32_t child_count = lv_obj_get_child_count(menu);

    for (uint32_t i = 0; i < child_count; ++i) {
        lv_obj_t* image = lv_obj_get_child(menu, static_cast<int32_t>(i));
        lv_area_t image_area = {};
        lv_obj_get_coords(image, &image_area);
        const int32_t image_center_x = (image_area.x1 + image_area.x2) / 2;
        const int32_t distance = LV_MIN(LV_ABS(image_center_x - menu_center_x), half_width);
        const int32_t scale = 300 - (110 * distance / LV_MAX(half_width, 1));
        const int32_t arc_offset = 8 + (34 * distance * distance /
                                        LV_MAX(half_width * half_width, 1));
        lv_image_set_scale(image, static_cast<uint32_t>(scale));
        lv_obj_set_style_translate_y(image, arc_offset, 0);
    }
}

/** 函数：菜单滚动时实时更新弧形排布；参数：event LVGL 事件；返回值：无 */
void WatchAppShell::MenuScrollCallback(lv_event_t* event) {
    LayoutMenu(lv_event_get_target_obj(event));
}

void WatchAppShell::UpdateClock() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time = {};
    localtime_r(&now, &local_time);
    char hour[3] = {};
    char minute[3] = {};
    char second[3] = {};
    std::snprintf(hour, sizeof(hour), "%02d", local_time.tm_hour);
    std::snprintf(minute, sizeof(minute), "%02d", local_time.tm_min);
    std::snprintf(second, sizeof(second), "%02d", local_time.tm_sec);
    lv_label_set_text(hour_label_, hour);
    lv_label_set_text(minute_label_, minute);
    lv_label_set_text(second_label_, second);
}

void WatchAppShell::ClockTimerCallback(lv_timer_t* timer) {
    auto* shell = static_cast<WatchAppShell*>(lv_timer_get_user_data(timer));
    if (shell != nullptr) {
        shell->UpdateClock();
    }
}

void WatchAppShell::MenuClickCallback(lv_event_t* event) {
    /* 后续逐个接入原 fs_create_* 实现；当前只记录索引，不改变原始菜单布局。 */
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    ESP_LOGI(kTag, "Watch menu selected: %u", static_cast<unsigned>(index));
}

void WatchAppShell::ShowXiaozhi() {
    if (xiaozhi_screen_ == nullptr || !lvgl_port_lock(1000)) {
        return;
    }
    lv_screen_load(xiaozhi_screen_);
    active_app_ = ActiveApp::kXiaozhi;
    lvgl_port_unlock();
}

void WatchAppShell::ShowWatch() {
    if (watch_screen_ == nullptr || !lvgl_port_lock(1000)) {
        return;
    }
    lv_screen_load(watch_screen_);
    active_app_ = ActiveApp::kWatch;
    lvgl_port_unlock();
}

void WatchAppShell::Toggle() {
    if (IsXiaozhiVisible()) {
        ShowWatch();
    } else {
        ShowXiaozhi();
    }
}
