#include "watch_app_shell.h"

#include "watch_countdown_service.h"
#include "watch_menu_assets.h"
#include "watch_ui_metrics.h"
#include "watch_weather_assets.h"

#include "boards/common/backlight.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <ctime>

LV_FONT_DECLARE(time_80);

namespace {
constexpr char kTag[] = "watch_app";
constexpr int kDisplayWidth = WatchUiMetrics::kWidth;
constexpr int kDisplayHeight = WatchUiMetrics::kHeight;
constexpr int kMenuWidth = WatchUiMetrics::kWidth;
constexpr int kMenuHeight = 100;
constexpr int kMenuIconSize = 60;
constexpr int kMenuIconBoxSize = 60;
constexpr int kMenuMinimumScale = 179;
constexpr int kMenuMaximumScale = 307;
constexpr size_t kMenuScaleLevelCount = 12;
constexpr int kBrightnessDrawerHiddenY = 268;
constexpr int kBrightnessDrawerVisibleY = 216;

lv_image_dsc_t s_menu_images[WATCH_MENU_ICON_COUNT];
lv_image_dsc_t s_menu_scaled_images[WATCH_MENU_ICON_COUNT][kMenuScaleLevelCount];
uint16_t* s_menu_scaled_pixels[WATCH_MENU_ICON_COUNT][kMenuScaleLevelCount] = {};
lv_image_dsc_t s_weather_images[WEATHER_FRAME_CNT];
bool s_images_initialized = false;
bool s_menu_scale_cache_ready = false;

/** 函数：为主页选择与 WMO 天气代码匹配的原版天气图标；参数：code WMO 代码；返回值：资源索引 */
size_t WeatherImageIndex(int code) {
    if (code == 0 || code == 1) return 6;
    if (code == 2) return 9;
    if (code == 3) return 7;
    if (code == 45 || code == 48) return 2;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 8;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 0;
    if (code >= 95 && code <= 99) return 1;
    return 9;
}

/** 函数：LVGL 动画执行器；参数：object 面板对象、y 目标纵坐标；返回值：无 */
void SetPanelY(void* object, int32_t y) {
    lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
}

/** 函数：以缓出动画移动系统面板；参数：panel 面板、target_y 目标纵坐标；返回值：无 */
void AnimatePanel(lv_obj_t* panel, int32_t target_y) {
    if (panel == nullptr) return;
    lv_anim_delete(panel, SetPanelY);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, panel);
    lv_anim_set_exec_cb(&animation, SetPanelY);
    lv_anim_set_values(&animation, lv_obj_get_y(panel), target_y);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

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
    /*
     * ESP32-S3 软件绘制器逐帧缩放 12 个 RGB565 图标会明显占用 CPU。将 190%~300%
     * （LVGL 以 256 表示原尺寸）的 12 个档位一次性预生成到 PSRAM，滚动时只更换图源。
     * 缓存申请失败则整体回退到 LVGL 实时缩放，避免出现部分图标尺寸不一致。
     */
    bool cache_complete = true;
    for (size_t icon = 0; icon < WATCH_MENU_ICON_COUNT && cache_complete; ++icon) {
        for (size_t level = 0; level < kMenuScaleLevelCount; ++level) {
            const int32_t scale = kMenuMinimumScale +
                static_cast<int32_t>((kMenuMaximumScale - kMenuMinimumScale) * level /
                                     (kMenuScaleLevelCount - 1));
            const int32_t dimension = (kMenuIconSize * scale + LV_SCALE_NONE / 2) / LV_SCALE_NONE;
            auto* pixels = static_cast<uint16_t*>(heap_caps_malloc(
                dimension * dimension * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (pixels == nullptr) {
                cache_complete = false;
                break;
            }
            for (int32_t y = 0; y < dimension; ++y) {
                const int32_t source_y = y * kMenuIconSize / dimension;
                for (int32_t x = 0; x < dimension; ++x) {
                    const int32_t source_x = x * kMenuIconSize / dimension;
                    pixels[y * dimension + x] =
                        watch_menu_pixels[icon][source_y * kMenuIconSize + source_x];
                }
            }
            s_menu_scaled_pixels[icon][level] = pixels;
            auto& image = s_menu_scaled_images[icon][level];
            image.header.magic = LV_IMAGE_HEADER_MAGIC;
            image.header.cf = LV_COLOR_FORMAT_RGB565;
            image.header.flags = 0;
            image.header.w = dimension;
            image.header.h = dimension;
            image.header.stride = dimension * sizeof(uint16_t);
            image.data_size = dimension * dimension * sizeof(uint16_t);
            image.data = reinterpret_cast<const uint8_t*>(pixels);
        }
    }
    if (!cache_complete) {
        for (size_t icon = 0; icon < WATCH_MENU_ICON_COUNT; ++icon) {
            for (size_t level = 0; level < kMenuScaleLevelCount; ++level) {
                heap_caps_free(s_menu_scaled_pixels[icon][level]);
                s_menu_scaled_pixels[icon][level] = nullptr;
            }
        }
        ESP_LOGW(kTag, "Menu scale cache unavailable; using LVGL software scaling");
    }
    s_menu_scale_cache_ready = cache_complete;
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

    /*
     * 物理 LCD 保持 480×320，只在中央建立原版 240×280 真实视口。此处不使用
     * transform scale，避免 LVGL 为缩放对象申请离屏图层并绘制整张大画布。
     */
    lv_obj_t* canvas = lv_obj_create(watch_screen_);
    watch_viewport_ = canvas;
    lv_obj_set_size(canvas, kDisplayWidth, kDisplayHeight);
    lv_obj_set_pos(canvas, WatchUiMetrics::kOffsetX, WatchUiMetrics::kOffsetY);
    lv_obj_set_style_bg_color(canvas, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_style_pad_all(canvas, 0, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    CreateClock(canvas);
    CreateWeatherWidget(canvas);
    CreateMenu(canvas);
    applications_.Initialize(canvas);
    CreateSystemLayers(canvas);
    lv_obj_add_event_cb(canvas, ScreenGestureCallback, LV_EVENT_GESTURE, this);

    /*
     * 对象级手势会被菜单、滑块等子对象截获，因此在指针输入设备上只监听按下与
     * 松开坐标，统一识别主页纵向手势。回调仍运行在 LVGL 任务中，不涉及 ISR。
     */
    for (lv_indev_t* input = lv_indev_get_next(nullptr); input != nullptr; input = lv_indev_get_next(input)) {
        if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) continue;
        lv_indev_add_event_cb(input, GlobalTouchCallback, LV_EVENT_PRESSED, this);
        lv_indev_add_event_cb(input, GlobalTouchCallback, LV_EVENT_RELEASED, this);
    }
    lv_display_t* display = lv_display_get_default();
    if (display != nullptr) {
        /* RENDER_READY 只在存在无效区域并完成绘制时触发，可反映真实绘制帧数。 */
        lv_display_add_event_cb(display, DisplayRenderCallback, LV_EVENT_RENDER_READY, this);
    }
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

    /* 完全复用原工程 AGENCYB 80 px 数字字体及坐标。 */
    lv_obj_set_style_text_font(hour_label_, &time_80, 0);
    lv_obj_set_style_text_font(minute_label_, &time_80, 0);
    lv_obj_set_style_text_font(second_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(hour_label_, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_align_to(minute_label_, hour_label_, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_align_to(second_label_, minute_label_, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
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
    lv_image_set_scale(weather_image_, LV_SCALE_NONE);
    /* 原版天气容器位于 (95,20)，图标在容器内偏移 (25,0)。 */
    lv_obj_set_pos(weather_image_, 120, 20);
    lv_obj_remove_flag(weather_image_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(weather_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weather_image_, WeatherClickCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(weather_image_, ScreenGestureCallback, LV_EVENT_GESTURE, this);

    weather_text_ = lv_label_create(parent);
    lv_label_set_text(weather_text_, "天气同步中");
    lv_obj_set_width(weather_text_, 135);
    lv_obj_set_style_text_color(weather_text_, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_align(weather_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(weather_text_, 95, 122);
}

void WatchAppShell::WeatherClickCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    if (shell != nullptr && shell->applications_.OpenWeather()) shell->BringSystemLayersToFront();
}

void WatchAppShell::CreateMenu(lv_obj_t* parent) {
    lv_obj_t* menu = lv_obj_create(parent);
    lv_obj_set_size(menu, kMenuWidth, kMenuHeight);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu, 0, 0);
    lv_obj_set_style_pad_all(menu, 0, 0);
    lv_obj_set_scroll_dir(menu, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(menu, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(menu, MenuScrollCallback, LV_EVENT_SCROLL, this);
    lv_obj_add_event_cb(menu, MenuScrollCallback, LV_EVENT_SCROLL_END, this);
    lv_obj_add_event_cb(menu, ScreenGestureCallback, LV_EVENT_GESTURE, this);

    for (size_t i = 0; i < WATCH_MENU_ICON_COUNT; ++i) {
        lv_obj_t* image = lv_image_create(menu);
        lv_image_set_src(image, &s_menu_images[i]);
        lv_obj_set_size(image, kMenuIconBoxSize, kMenuIconBoxSize);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
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
 * 函    数：创建顶部状态、底部亮度抽屉和临时通知层
 * 参    数：parent 主页画布，所有权由 LVGL 管理
 * 返 回 值：无
 * 注意事项：三个层均只在 LVGL 任务中更新；电量读取来自当前板级 PMIC。
 */
void WatchAppShell::CreateSystemLayers(lv_obj_t* parent) {
    (void)parent;
    /*
     * 状态栏与应用 overlay 同为 Screen 的直接子对象，应用打开后可重新提升到最前方。
     * 如果继续挂在主页 canvas 内，全屏应用会按 LVGL 的兄弟层级规则把它完全遮住。
     */
    status_bar_ = lv_obj_create(watch_viewport_);
    lv_obj_set_size(status_bar_, kDisplayWidth, WatchUiMetrics::kStatusBarHeight);
    lv_obj_set_pos(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_CLICKABLE);

    /* 原手表源码的状态信息使用 Montserrat 12，数字更紧凑，笔画比例也更协调。 */
    status_time_label_ = lv_label_create(status_bar_);
    lv_label_set_text(status_time_label_, "00:00:00");
    lv_obj_set_style_text_color(status_time_label_, lv_color_white(), 0);
    /* 时间使用同属原项目 Montserrat 字族的 16 px 字号，避免 480 px 屏幕上 12 px 笔画过细。 */
    lv_obj_set_style_text_font(status_time_label_, &lv_font_montserrat_12, 0);
    lv_obj_align(status_time_label_, LV_ALIGN_TOP_LEFT, 5, 5);

    /* 电量采用独立图形，不依赖符号字体，避免电池字符缺字后整块状态消失。 */
    battery_area_ = lv_obj_create(status_bar_);
    lv_obj_set_size(battery_area_, 24, 12);
    lv_obj_align(battery_area_, LV_ALIGN_TOP_RIGHT, -4, 6);
    lv_obj_set_style_bg_opa(battery_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_area_, 1, 0);
    lv_obj_set_style_border_color(battery_area_, lv_color_white(), 0);
    lv_obj_set_style_radius(battery_area_, 1, 0);
    lv_obj_set_style_pad_all(battery_area_, 0, 0);
    lv_obj_remove_flag(battery_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(battery_area_, LV_OBJ_FLAG_CLICKABLE);

    battery_tip_ = lv_obj_create(status_bar_);
    lv_obj_set_size(battery_tip_, 2, 5);
    lv_obj_align_to(battery_tip_, battery_area_, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(battery_tip_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(battery_tip_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_tip_, 0, 0);
    lv_obj_set_style_radius(battery_tip_, 1, 0);
    lv_obj_remove_flag(battery_tip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(battery_tip_, LV_OBJ_FLAG_CLICKABLE);
    battery_bar_ = lv_obj_create(battery_area_);
    lv_obj_set_height(battery_bar_, LV_PCT(100));
    lv_obj_set_width(battery_bar_, 0);
    lv_obj_align(battery_bar_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(battery_bar_, 0, 0);
    lv_obj_set_style_radius(battery_bar_, 1, 0);
    lv_obj_remove_flag(battery_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(battery_bar_, LV_OBJ_FLAG_CLICKABLE);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "--%");
    lv_obj_set_style_text_color(battery_label_, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_text_font(battery_label_, &lv_font_montserrat_12, 0);
    lv_obj_align_to(battery_label_, battery_area_, LV_ALIGN_OUT_LEFT_MID, -3, 0);

    fps_label_ = lv_label_create(status_bar_);
    lv_label_set_text(fps_label_, "0 FPS");
    lv_obj_set_style_text_color(fps_label_, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(fps_label_, &lv_font_montserrat_12, 0);
    lv_obj_align_to(fps_label_, battery_label_, LV_ALIGN_OUT_LEFT_MID, -4, 0);

    brightness_drawer_ = lv_obj_create(watch_viewport_);
    lv_obj_set_size(brightness_drawer_, 228, 64);
    /* 隐藏时仍保留 12 px 抓手，复用原工程“可拖动容器”的交互方式。 */
    lv_obj_set_pos(brightness_drawer_, 6, kBrightnessDrawerHiddenY);
    lv_obj_set_style_radius(brightness_drawer_, 18, 0);
    lv_obj_set_style_bg_color(brightness_drawer_, lv_color_hex(0x172033), 0);
    lv_obj_set_style_bg_opa(brightness_drawer_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(brightness_drawer_, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(brightness_drawer_, 1, 0);
    lv_obj_remove_flag(brightness_drawer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* drawer_handle = lv_obj_create(brightness_drawer_);
    lv_obj_set_size(drawer_handle, 54, 4);
    lv_obj_align(drawer_handle, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(drawer_handle, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_border_width(drawer_handle, 0, 0);
    lv_obj_set_style_radius(drawer_handle, 3, 0);
    lv_obj_remove_flag(drawer_handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(drawer_handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* brightness_title = lv_label_create(brightness_drawer_);
    lv_label_set_text(brightness_title, LV_SYMBOL_EYE_OPEN "  屏幕亮度");
    lv_obj_set_style_text_color(brightness_title, lv_color_white(), 0);
    lv_obj_align(brightness_title, LV_ALIGN_LEFT_MID, 4, 2);
    brightness_slider_ = lv_slider_create(brightness_drawer_);
    lv_obj_set_size(brightness_slider_, 142, 14);
    lv_obj_align(brightness_slider_, LV_ALIGN_RIGHT_MID, -4, 2);
    lv_slider_set_range(brightness_slider_, 5, 100);
    Backlight* backlight = Board::GetInstance().GetBacklight();
    lv_slider_set_value(brightness_slider_, backlight == nullptr ? 80 : backlight->brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider_, BrightnessCallback, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(brightness_slider_, BrightnessCallback, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(brightness_drawer_, ScreenGestureCallback, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(brightness_drawer_, BrightnessDrawerTouchCallback, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(brightness_drawer_, BrightnessDrawerTouchCallback, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(brightness_drawer_, BrightnessDrawerTouchCallback, LV_EVENT_RELEASED, this);

    /* 通知层挂在屏幕根节点上，应用全屏展开后仍可移动到最前方显示。 */
    notification_panel_ = lv_obj_create(watch_viewport_);
    lv_obj_set_size(notification_panel_, 228, 44);
    lv_obj_set_pos(notification_panel_, 6, -48);
    lv_obj_set_style_radius(notification_panel_, 16, 0);
    lv_obj_set_style_bg_color(notification_panel_, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_bg_opa(notification_panel_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(notification_panel_, lv_color_hex(0x60a5fa), 0);
    lv_obj_set_style_border_width(notification_panel_, 1, 0);
    lv_obj_remove_flag(notification_panel_, LV_OBJ_FLAG_SCROLLABLE);
    notification_label_ = lv_label_create(notification_panel_);
    lv_obj_set_width(notification_label_, 206);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);
    lv_obj_center(notification_label_);
    lv_obj_move_foreground(notification_panel_);
    BringSystemLayersToFront();
    UpdateStatusBar();
}

/**
 * 函    数：把全局状态栏、亮度抽屉和通知层提升到应用覆盖层之上
 * 参    数：无
 * 返 回 值：无
 * 注意事项：仅在 LVGL 任务或持有 LVGL 锁时调用；调用顺序决定重叠区域的显示优先级。
 */
void WatchAppShell::BringSystemLayersToFront() {
    if (status_bar_ != nullptr) lv_obj_move_foreground(status_bar_);
    if (brightness_drawer_ != nullptr) lv_obj_move_foreground(brightness_drawer_);
    if (notification_panel_ != nullptr) lv_obj_move_foreground(notification_panel_);
}

void WatchAppShell::ShowBrightnessDrawer(bool visible) {
    brightness_drawer_visible_ = visible;
    if (visible) lv_obj_move_foreground(brightness_drawer_);
    AnimatePanel(brightness_drawer_, visible ? kBrightnessDrawerVisibleY : kBrightnessDrawerHiddenY);
}

void WatchAppShell::ShowNotification(const char* text) {
    if (notification_label_ == nullptr || text == nullptr) return;
    lv_label_set_text(notification_label_, text);
    lv_obj_move_foreground(notification_panel_);
    AnimatePanel(notification_panel_, 4);
    notification_deadline_us_ = esp_timer_get_time() + 3500000LL;
}

void WatchAppShell::ScreenGestureCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    if (shell == nullptr || shell->applications_.IsOpen()) return;
    lv_indev_t* input = lv_indev_active();
    if (input == nullptr) return;
    const lv_dir_t direction = lv_indev_get_gesture_dir(input);
    if (direction == LV_DIR_TOP) {
        shell->ShowBrightnessDrawer(true);
    } else if (direction == LV_DIR_BOTTOM) {
        if (shell->brightness_drawer_visible_) shell->ShowBrightnessDrawer(false);
        else shell->ShowNotification("暂无新通知");
    }
}

/**
 * 函    数：在输入设备层识别主页纵向滑动
 * 参    数：event LVGL 输入设备事件，用户数据必须为有效的 WatchAppShell
 * 返 回 值：无
 * 注意事项：仅记录按下、松开两个坐标；所有 UI 操作均在 LVGL 任务上下文执行。
 */
void WatchAppShell::GlobalTouchCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    auto* input = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (shell == nullptr || input == nullptr) return;

    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        shell->global_touch_start_ = point;
        shell->global_touch_tracking_ =
            shell->active_app_ == ActiveApp::kWatch && !shell->applications_.IsOpen() &&
            point.x >= WatchUiMetrics::kOffsetX &&
            point.x < WatchUiMetrics::kOffsetX + WatchUiMetrics::kWidth &&
            point.y >= WatchUiMetrics::kOffsetY &&
            point.y < WatchUiMetrics::kOffsetY + WatchUiMetrics::kHeight;
        return;
    }
    if (code != LV_EVENT_RELEASED || !shell->global_touch_tracking_) return;
    shell->global_touch_tracking_ = false;

    constexpr int32_t kMinimumVerticalSwipe = 32;
    constexpr int32_t kDirectionMargin = 12;
    const int32_t delta_x = point.x - shell->global_touch_start_.x;
    const int32_t delta_y = point.y - shell->global_touch_start_.y;

    /*
     * 抽屉展开后优先识别向下收回动作。此分支不要求严格垂直，避免手指落在滑块或
     * 抽屉子控件上时，轻微横向偏移导致全局手势被方向裕量过滤。
     */
    constexpr int32_t kDrawerCloseSwipe = 16;
    if (shell->brightness_drawer_visible_ && delta_y >= kDrawerCloseSwipe) {
        shell->ShowBrightnessDrawer(false);
        return;
    }
    if (LV_ABS(delta_y) < kMinimumVerticalSwipe ||
        LV_ABS(delta_y) <= LV_ABS(delta_x) + kDirectionMargin) return;

    if (delta_y < 0) {
        shell->ShowBrightnessDrawer(true);
    } else if (shell->brightness_drawer_visible_) {
        shell->ShowBrightnessDrawer(false);
    } else {
        shell->ShowNotification("暂无新通知");
    }
}

/**
 * 函    数：累计 LVGL 实际完成绘制的帧数
 * 参    数：event 显示器 RENDER_READY 事件
 * 返 回 值：无
 * 注意事项：该回调与状态栏定时器同属 LVGL 任务，不需要额外互斥。
 */
void WatchAppShell::DisplayRenderCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    if (shell != nullptr) ++shell->rendered_frame_count_;
}

void WatchAppShell::BrightnessDrawerTouchCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    lv_indev_t* input = lv_indev_active();
    if (shell == nullptr || input == nullptr) return;
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        shell->brightness_touch_last_y_ = point.y;
        lv_anim_delete(shell->brightness_drawer_, SetPanelY);
    } else if (code == LV_EVENT_PRESSING) {
        const int32_t delta = point.y - shell->brightness_touch_last_y_;
        shell->brightness_touch_last_y_ = point.y;
        const int32_t target = LV_CLAMP(kBrightnessDrawerVisibleY,
                                        lv_obj_get_y(shell->brightness_drawer_) + delta,
                                        kBrightnessDrawerHiddenY);
        lv_obj_set_y(shell->brightness_drawer_, target);
    } else if (code == LV_EVENT_RELEASED) {
        const int32_t midpoint = (kBrightnessDrawerVisibleY + kBrightnessDrawerHiddenY) / 2;
        shell->ShowBrightnessDrawer(lv_obj_get_y(shell->brightness_drawer_) < midpoint);
    }
}

void WatchAppShell::BrightnessCallback(lv_event_t* event) {
    Backlight* backlight = Board::GetInstance().GetBacklight();
    if (backlight == nullptr) return;
    const uint8_t brightness = static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target_obj(event)));
    backlight->SetBrightness(brightness, lv_event_get_code(event) == LV_EVENT_RELEASED);
}

void WatchAppShell::UpdateStatusBar() {
    if (battery_label_ == nullptr || battery_bar_ == nullptr) return;
    if (status_time_label_ != nullptr) {
        const std::time_t now = std::time(nullptr);
        std::tm local_time = {};
        localtime_r(&now, &local_time);
        lv_label_set_text_fmt(status_time_label_, "%02d:%02d:%02d",
                              local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
    }
    int level = 0;
    bool charging = false;
    bool discharging = false;
    if (Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
        const int clamped_level = LV_CLAMP(0, level, 100);
        lv_label_set_text_fmt(battery_label_, "%d%%", clamped_level);
        lv_obj_set_width(battery_bar_, LV_MAX(1, clamped_level * 22 / 100));
        const lv_color_t color = charging ? lv_color_hex(0x38bdf8)
            : clamped_level <= 20 ? lv_color_hex(0xef4444)
            : clamped_level <= 30 ? lv_color_hex(0xf59e0b)
                                  : lv_color_hex(0x22c55e);
        lv_obj_set_style_bg_color(battery_bar_, color, 0);
    } else {
        lv_label_set_text(battery_label_, "--%");
        lv_obj_set_width(battery_bar_, 1);
        lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x64748b), 0);
    }
    if (fps_label_ != nullptr) {
        rendered_fps_ = rendered_frame_count_;
        rendered_frame_count_ = 0;
        lv_label_set_text_fmt(fps_label_, "%lu FPS", static_cast<unsigned long>(rendered_fps_));
    }
}

/** 函数：把天气缓存同步到主页图标和温度文字；参数：无；返回值：无 */
void WatchAppShell::UpdateWeatherWidget() {
    WatchApplications::WeatherSummary summary;
    if (!applications_.GetCurrentWeather(&summary)) return;
    if (weather_image_ != nullptr && last_weather_code_ != summary.code) {
        lv_image_set_src(weather_image_, &s_weather_images[WeatherImageIndex(summary.code)]);
        last_weather_code_ = summary.code;
    }
    if (weather_text_ != nullptr) {
        lv_label_set_text_fmt(weather_text_, "%.1f° / %.1f°",
                              static_cast<double>(summary.maximum_tenths) / 10.0,
                              static_cast<double>(summary.minimum_tenths) / 10.0);
    }
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
        /*
         * 屏外图标不会参与本帧绘制，也无需计算软件缩放。预留一个图标宽度作为进入视口前的
         * 更新区，保证图标滑入时已经取得正确的弧线位置。
         */
        if (image_center_x < menu_area.x1 - kMenuIconSize ||
            image_center_x > menu_area.x2 + kMenuIconSize) {
            continue;
        }
        const int32_t distance = LV_MIN(LV_ABS(image_center_x - menu_center_x), half_width);
        const int32_t scale = kMenuMaximumScale -
            ((kMenuMaximumScale - kMenuMinimumScale) * distance / LV_MAX(half_width, 1));
        const int32_t arc_offset = 4 + (24 * distance * distance /
                                        LV_MAX(half_width * half_width, 1));
        if (s_menu_scale_cache_ready) {
            const size_t icon = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(image));
            const size_t level = static_cast<size_t>(LV_CLAMP(
                0, (scale - kMenuMinimumScale) * static_cast<int32_t>(kMenuScaleLevelCount - 1) /
                       (kMenuMaximumScale - kMenuMinimumScale),
                static_cast<int32_t>(kMenuScaleLevelCount - 1)));
            const auto* source = &s_menu_scaled_images[icon][level];
            if (lv_image_get_src(image) != source) lv_image_set_src(image, source);
        } else if (LV_ABS(lv_image_get_scale(image) - scale) >= 2) {
            /* setter 会使旧、新区域同时失效；数值未变化时跳过可显著减少重绘面积。 */
            lv_image_set_scale(image, static_cast<uint32_t>(scale));
        }
        if (lv_obj_get_style_translate_y(image, LV_PART_MAIN) != arc_offset) {
            lv_obj_set_style_translate_y(image, arc_offset, 0);
        }
    }
}

/** 函数：菜单滚动时实时更新弧形排布；参数：event LVGL 事件；返回值：无 */
void WatchAppShell::MenuScrollCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    if (shell == nullptr) return;
    const uint32_t now = lv_tick_get();
    const bool scroll_finished = lv_event_get_code(event) == LV_EVENT_SCROLL_END;
    if (!scroll_finished && lv_tick_elaps(shell->last_menu_layout_tick_) < 10U) return;
    shell->last_menu_layout_tick_ = now;
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
    UpdateStatusBar();
    UpdateWeatherWidget();
    applications_.RefreshWeatherIfStale();
    if (WatchCountdownService::Instance().TakeFinishedEvent()) ShowNotification("倒计时结束");
    if (notification_deadline_us_ != 0 && esp_timer_get_time() >= notification_deadline_us_) {
        AnimatePanel(notification_panel_, -48);
        notification_deadline_us_ = 0;
    }
}

void WatchAppShell::ClockTimerCallback(lv_timer_t* timer) {
    auto* shell = static_cast<WatchAppShell*>(lv_timer_get_user_data(timer));
    if (shell != nullptr) {
        shell->UpdateClock();
    }
}

void WatchAppShell::MenuClickCallback(lv_event_t* event) {
    auto* shell = static_cast<WatchAppShell*>(lv_event_get_user_data(event));
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (shell != nullptr) {
        if (!shell->applications_.Open(index)) {
            ESP_LOGW(kTag, "Failed to open watch app index=%u", static_cast<unsigned>(index));
        } else {
            shell->BringSystemLayersToFront();
        }
    }
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

bool WatchAppShell::HandleBootClick() {
    if (!lvgl_port_lock(1000)) return false;
    const bool handled = active_app_ == ActiveApp::kWatch && applications_.IsOpen();
    if (handled) applications_.Close();
    lvgl_port_unlock();
    return handled;
}
