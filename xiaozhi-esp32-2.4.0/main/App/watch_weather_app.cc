#include "watch_apps.h"

#include "boards/common/board.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "watch_weather_assets.h"
#include "watch_ui_metrics.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr char kTag[] = "watch_weather";
constexpr size_t kMaximumResponseBytes = 32U * 1024U;
constexpr int32_t kWeatherPanelWidth = WatchUiMetrics::kContentWidth;
constexpr int32_t kWeatherDayCardWidth = 90;
constexpr int32_t kWeatherEdgePadding = (kWeatherPanelWidth - kWeatherDayCardWidth) / 2;
constexpr int32_t kWeatherIconSize = 48;

lv_image_dsc_t s_weather_images[WEATHER_FRAME_CNT];
uint16_t* s_weather_image_pixels[WEATHER_FRAME_CNT] = {};
bool s_weather_images_ready = false;

/** 函数：取得小智主题中文字库；参数：无；返回值：有效字体 */
const lv_font_t* GetWeatherFont() {
    Display* display = Board::GetInstance().GetDisplay();
    if (display == nullptr || display->GetTheme() == nullptr) return LV_FONT_DEFAULT;
    auto* theme = static_cast<LvglTheme*>(display->GetTheme());
    if (theme->text_font() == nullptr || theme->text_font()->font() == nullptr) return LV_FONT_DEFAULT;
    return theme->text_font()->font();
}

/** 函数：包装原 Arduino RGB565 天气资源；参数：无；返回值：无 */
void InitializeWeatherImages() {
    if (s_weather_images_ready) return;
    for (size_t index = 0; index < WEATHER_FRAME_CNT; ++index) {
        auto& image = s_weather_images[index];
        /*
         * 原图为 100×100，而页面实际只显示 55×55。若每次滑动都让 LVGL 做软件缩放，
         * 7 个图标会在每一帧重复执行采样。这里在进入天气页时只预缩放一次并放入
         * PSRAM，滑动阶段直接拷贝 RGB565 像素；申请失败时仍回退到原图，功能不受影响。
         */
        auto* scaled = static_cast<uint16_t*>(heap_caps_malloc(
            kWeatherIconSize * kWeatherIconSize * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (scaled != nullptr) {
            for (int32_t y = 0; y < kWeatherIconSize; ++y) {
                const int32_t source_y = y * WEATHER_IMG_HEIGHT / kWeatherIconSize;
                for (int32_t x = 0; x < kWeatherIconSize; ++x) {
                    const int32_t source_x = x * WEATHER_IMG_WIDTH / kWeatherIconSize;
                    scaled[y * kWeatherIconSize + x] =
                        watch_weather_pixels[index][source_y * WEATHER_IMG_WIDTH + source_x];
                }
            }
            s_weather_image_pixels[index] = scaled;
        }
        const bool use_scaled_image = scaled != nullptr;
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_RGB565;
        image.header.w = use_scaled_image ? kWeatherIconSize : WEATHER_IMG_WIDTH;
        image.header.h = use_scaled_image ? kWeatherIconSize : WEATHER_IMG_HEIGHT;
        image.header.stride = image.header.w * sizeof(uint16_t);
        image.data_size = image.header.w * image.header.h * sizeof(uint16_t);
        image.data = reinterpret_cast<const uint8_t*>(
            use_scaled_image ? scaled : watch_weather_pixels[index]);
    }
    s_weather_images_ready = true;
}

/** 函数：把 WMO 天气代码映射到原工程图标；参数：code；返回值：图标索引 */
int WeatherImageIndex(int code) {
    if (code == 0 || code == 1) return 6;
    if (code == 2) return 9;
    if (code == 3) return 7;
    if (code == 45 || code == 48) return 2;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 8;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 0;
    if (code >= 95 && code <= 99) return 1;
    return 9;
}

/** 函数：取得原工程中文天气说明；参数：WMO code；返回值：静态字符串 */
const char* WeatherDescription(int code) {
    switch (code) {
        case 0: return "晴";
        case 1: return "大部晴";
        case 2: return "多云";
        case 3: return "阴";
        case 45: case 48: return "雾";
        case 51: case 53: case 55: return "毛毛雨";
        case 56: case 57: return "冻毛毛雨";
        case 61: return "小雨";
        case 63: return "中雨";
        case 65: return "大雨";
        case 66: case 67: return "冻雨";
        case 71: return "小雪";
        case 73: return "中雪";
        case 75: return "大雪";
        case 77: return "雪粒";
        case 80: case 81: return "阵雨";
        case 82: return "强阵雨";
        case 85: return "阵雪";
        case 86: return "强阵雪";
        case 95: return "雷暴";
        case 96: case 99: return "雷暴冰雹";
        default: return "未知";
    }
}

/** 函数：从 JSON 数组安全读取数字；参数：数组、索引、输出；返回值：是否成功 */
bool ReadNumber(const cJSON* array, int index, double* result) {
    if (array == nullptr || result == nullptr) return false;
    const cJSON* item = cJSON_GetArrayItem(array, index);
    if (!cJSON_IsNumber(item)) return false;
    *result = item->valuedouble;
    return true;
}
}  // namespace

void WatchApplications::CreateWeather() {
    InitializeWeatherImages();
    lv_obj_t* title = lv_label_create(overlay_);
    lv_label_set_text(title, "未来七天天气");
    lv_obj_set_style_text_font(title, GetWeatherFont(), 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t* refresh = lv_button_create(overlay_);
    lv_obj_set_size(refresh, 44, 32);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -6, 28);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x2563eb), 0);
    lv_obj_set_style_radius(refresh, 16, 0);
    lv_obj_set_style_border_width(refresh, 0, 0);
    lv_obj_t* refresh_icon = lv_label_create(refresh);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(refresh_icon, &lv_font_montserrat_14, 0);
    lv_obj_center(refresh_icon);
    lv_obj_add_event_cb(refresh, WeatherRefreshCallback, LV_EVENT_CLICKED, this);

    weather_panel_ = lv_obj_create(overlay_);
    lv_obj_set_size(weather_panel_, kWeatherPanelWidth, 214);
    lv_obj_set_pos(weather_panel_, 6, 62);
    lv_obj_set_style_pad_all(weather_panel_, 0, 0);
    /*
     * 左右各保留 180 px，使 100 px 宽的第一天和第七天都能滑到 460 px 视口中央。
     * 没有这段边缘留白时，滚动范围会在第六天附近提前到达上限。
     */
    lv_obj_set_style_pad_left(weather_panel_, kWeatherEdgePadding, 0);
    lv_obj_set_style_pad_right(weather_panel_, kWeatherEdgePadding, 0);
    lv_obj_set_style_border_width(weather_panel_, 0, 0);
    /*
     * 与原源码一致，滚动容器保持透明且不做圆角裁剪。460×252 圆角遮罩会让
     * 软件绘制器在每一帧遍历大面积像素，是横向滑动时最明显的额外开销。
     */
    lv_obj_set_style_radius(weather_panel_, 0, 0);
    lv_obj_set_style_bg_opa(weather_panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(weather_panel_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(weather_panel_, LV_SCROLLBAR_MODE_OFF);
    /* 保留惯性滚动和卡片吸附，关闭边界回弹以减少无效的全区域重绘。 */
    lv_obj_remove_flag(weather_panel_, LV_OBJ_FLAG_SCROLL_ELASTIC);

    weather_status_ = lv_label_create(weather_panel_);
    lv_label_set_text(weather_status_, "正在获取天气…");
    lv_obj_set_style_text_font(weather_status_, GetWeatherFont(), 0);
    lv_obj_set_style_text_color(weather_status_, lv_color_hex(0xcbd5e1), 0);
    lv_obj_center(weather_status_);

    /* 先显示现有预报，只在缓存过期时后台刷新，避免滑动期间与 TLS 任务抢占 CPU。 */
    weather_rendered_state_ = WeatherState::kIdle;
    UpdateWeather();
    RefreshWeatherIfStale();
}

void WatchApplications::StartWeatherFetch() {
    if (weather_task_.load() != nullptr) return;

    /*
     * LVGL 界面会早于 Application::StartNetwork() 创建。若在 LwIP 邮箱建立前
     * 调用 getaddrinfo()，ESP-IDF 会以 "Invalid mbox" 断言复位。因此必须同时
     * 确认 Wi-Fi 管理器已初始化且 STA 已获得连接，断网期间保留缓存数据，
     * 由主页定时器在后续周期再尝试。
     */
    auto& wifi_manager = WifiManager::GetInstance();
    if (!wifi_manager.IsInitialized() || !wifi_manager.IsConnected()) return;

    weather_cancel_.store(false);
    weather_last_attempt_us_ = esp_timer_get_time();
    weather_rendered_state_ = WeatherState::kIdle;
    weather_state_.store(WeatherState::kLoading);
    if (weather_status_ != nullptr) lv_label_set_text(weather_status_, "正在获取天气…");
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(WeatherTaskEntry, "watch_weather", 7168, this, 3, &handle, 0) != pdPASS) {
        weather_state_.store(WeatherState::kError);
        return;
    }
    weather_task_.store(handle);
}

void WatchApplications::WeatherTaskEntry(void* parameter) {
    auto* self = static_cast<WatchApplications*>(parameter);
    /* 等待创建方发布任务句柄，防止极快失败路径先清空、随后又被旧句柄覆盖。 */
    while (self->weather_task_.load() == nullptr && !self->weather_cancel_.load()) {
        vTaskDelay(1);
    }
    bool success = false;
    std::array<WeatherDay, 7> fetched_days{};
    size_t fetched_count = 0;
    Settings settings("watch", false);
    const int32_t latitude_e4 = settings.GetInt("weather_lat_e4", 312300);
    const int32_t longitude_e4 = settings.GetInt("weather_lon_e4", 1214700);
    char url[384] = {};
    std::snprintf(url, sizeof(url),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=7",
                  latitude_e4 / 10000.0, longitude_e4 / 10000.0);

    NetworkInterface* network = Board::GetInstance().GetNetwork();
    if (network != nullptr && !self->weather_cancel_.load()) {
        auto http = network->CreateHttp(3);
        if (http != nullptr) http->SetTimeout(8000);
        if (http != nullptr && http->Open("GET", url) && http->GetStatusCode() == 200) {
            std::string body = http->ReadAll();
            http->Close();
            if (body.size() <= kMaximumResponseBytes) {
                cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
                cJSON* daily = root == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(root, "daily");
                cJSON* dates = daily == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(daily, "time");
                cJSON* codes = daily == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
                cJSON* maximums = daily == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
                cJSON* minimums = daily == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
                const int count = std::min({7, cJSON_GetArraySize(dates), cJSON_GetArraySize(codes),
                                            cJSON_GetArraySize(maximums), cJSON_GetArraySize(minimums)});
                size_t valid = 0;
                for (int index = 0; index < count && !self->weather_cancel_.load(); ++index) {
                    cJSON* date = cJSON_GetArrayItem(dates, index);
                    double code = 0.0;
                    double maximum = 0.0;
                    double minimum = 0.0;
                    if (!cJSON_IsString(date) || date->valuestring == nullptr || std::strlen(date->valuestring) < 10 ||
                        !ReadNumber(codes, index, &code) || !ReadNumber(maximums, index, &maximum) ||
                        !ReadNumber(minimums, index, &minimum)) continue;
                    WeatherDay& day = fetched_days[valid++];
                    std::memcpy(day.date.data(), date->valuestring + 5, 5);
                    day.date[5] = '\0';
                    day.code = static_cast<int16_t>(std::lround(code));
                    day.maximum_tenths = static_cast<int16_t>(std::lround(maximum * 10.0));
                    day.minimum_tenths = static_cast<int16_t>(std::lround(minimum * 10.0));
                }
                fetched_count = valid;
                success = valid != 0 && !self->weather_cancel_.load();
                cJSON_Delete(root);
            }
        } else if (http != nullptr) {
            http->Close();
        }
    }
    if (success) {
        {
            std::lock_guard<std::mutex> lock(self->weather_data_mutex_);
            self->weather_days_ = fetched_days;
            self->weather_day_count_ = fetched_count;
        }
        self->SaveWeatherCache();
    }
    bool has_cache = false;
    {
        std::lock_guard<std::mutex> lock(self->weather_data_mutex_);
        has_cache = self->weather_day_count_ != 0;
    }
    self->weather_state_.store(success || has_cache ? WeatherState::kReady : WeatherState::kError);
    self->weather_task_.store(nullptr);
    ESP_LOGI(kTag, "Weather request finished: %s", success ? "ok" : "failed");
    vTaskDelete(nullptr);
}

/**
 * 函    数：加载最近一次当日天气缓存
 * 参    数：无
 * 返 回 值：无
 * 注意事项：缓存只用于开机和断网占位，联网成功后会被完整七日预报替换。
 */
void WatchApplications::LoadWeatherCache() {
    Settings settings("watch", false);
    if (!settings.GetBool("w_valid", false)) return;
    WeatherDay cached{};
    const std::string date = settings.GetString("w_date");
    if (date.size() == 5) std::memcpy(cached.date.data(), date.data(), 5);
    cached.code = static_cast<int16_t>(settings.GetInt("w_code", 3));
    cached.maximum_tenths = static_cast<int16_t>(settings.GetInt("w_max10", 0));
    cached.minimum_tenths = static_cast<int16_t>(settings.GetInt("w_min10", 0));
    std::lock_guard<std::mutex> lock(weather_data_mutex_);
    weather_days_[0] = cached;
    weather_day_count_ = 1;
    weather_state_.store(WeatherState::kReady);
}

/** 函数：保存当日天气供断网启动使用；参数：无；返回值：无；可由天气任务调用 */
void WatchApplications::SaveWeatherCache() {
    WeatherDay current{};
    {
        std::lock_guard<std::mutex> lock(weather_data_mutex_);
        if (weather_day_count_ == 0) return;
        current = weather_days_[0];
    }
    Settings settings("watch", true);
    settings.SetBool("w_valid", true);
    settings.SetString("w_date", current.date.data());
    settings.SetInt("w_code", current.code);
    settings.SetInt("w_max10", current.maximum_tenths);
    settings.SetInt("w_min10", current.minimum_tenths);
}

bool WatchApplications::GetCurrentWeather(WeatherSummary* summary) {
    if (summary == nullptr) return false;
    std::lock_guard<std::mutex> lock(weather_data_mutex_);
    if (weather_day_count_ == 0) return false;
    summary->date = weather_days_[0].date;
    summary->maximum_tenths = weather_days_[0].maximum_tenths;
    summary->minimum_tenths = weather_days_[0].minimum_tenths;
    summary->code = weather_days_[0].code;
    return true;
}

void WatchApplications::RefreshWeatherIfStale() {
    if (weather_task_.load() != nullptr) return;
    const int64_t now_us = esp_timer_get_time();
    const WeatherState state = weather_state_.load();
    const int64_t retry_us = state == WeatherState::kReady ? 30LL * 60LL * 1000000LL : 60LL * 1000000LL;
    if (weather_last_attempt_us_ == 0 || now_us - weather_last_attempt_us_ >= retry_us) StartWeatherFetch();
}

void WatchApplications::UpdateWeather() {
    const WeatherState state = weather_state_.load();
    if (state == weather_rendered_state_) return;
    weather_rendered_state_ = state;
    if (state == WeatherState::kReady) {
        RenderWeather();
    } else if (state == WeatherState::kError && weather_status_ != nullptr) {
        lv_label_set_text(weather_status_, "天气获取失败\n请检查网络或在设置中修改经纬度");
        lv_obj_set_style_text_align(weather_status_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(weather_status_);
    }
}

void WatchApplications::RenderWeather() {
    if (weather_panel_ == nullptr) return;
    std::array<WeatherDay, 7> days{};
    size_t day_count = 0;
    {
        std::lock_guard<std::mutex> lock(weather_data_mutex_);
        days = weather_days_;
        day_count = weather_day_count_;
    }
    if (day_count == 0) return;
    lv_obj_clean(weather_panel_);
    weather_status_ = nullptr;

    for (size_t index = 0; index < day_count; ++index) {
        const WeatherDay& day = days[index];
        const int x = static_cast<int>(index * kWeatherDayCardWidth);
        /*
         * 每一天必须是滚动面板的直接子对象，LVGL 的居中吸附才能按“天”计算位置。
         * 旧实现只有一个 700 px 宽的直接子对象，松手后总会吸回同一个中心点。
         */
        lv_obj_t* day_card = lv_obj_create(weather_panel_);
        lv_obj_set_size(day_card, kWeatherDayCardWidth, 214);
        lv_obj_set_pos(day_card, x, 0);
        lv_obj_set_style_bg_opa(day_card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(day_card, 0, 0);
        lv_obj_set_style_pad_all(day_card, 0, 0);
        lv_obj_remove_flag(day_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(day_card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(day_card, LV_OBJ_FLAG_SNAPPABLE);

        lv_obj_t* image = lv_image_create(day_card);
        lv_image_set_src(image, &s_weather_images[WeatherImageIndex(day.code)]);
        if (s_weather_images[WeatherImageIndex(day.code)].header.w == kWeatherIconSize) {
            lv_obj_set_pos(image, (kWeatherDayCardWidth - kWeatherIconSize) / 2, 4);
        } else {
            /* PSRAM 不足时保留旧的软件缩放回退路径。 */
            lv_image_set_scale(image, 123);
            lv_obj_set_pos(image, -4, -20);
        }
        lv_obj_t* description = lv_label_create(day_card);
        lv_label_set_text(description, WeatherDescription(day.code));
        lv_obj_set_width(description, kWeatherDayCardWidth);
        lv_obj_set_style_text_font(description, GetWeatherFont(), 0);
        lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(description, lv_color_white(), 0);
        lv_obj_set_pos(description, 0, 50);
        lv_obj_t* maximum = lv_label_create(day_card);
        lv_label_set_text_fmt(maximum, "最高 %.1f°", day.maximum_tenths / 10.0);
        lv_obj_set_width(maximum, kWeatherDayCardWidth);
        lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(maximum, lv_color_hex(0xfef08a), 0);
        lv_obj_set_pos(maximum, 0, 80);
        lv_obj_t* minimum = lv_label_create(day_card);
        lv_label_set_text_fmt(minimum, "最低 %.1f°", day.minimum_tenths / 10.0);
        lv_obj_set_width(minimum, kWeatherDayCardWidth);
        lv_obj_set_style_text_align(minimum, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(minimum, lv_color_hex(0xbfdbfe), 0);
        lv_obj_set_pos(minimum, 0, 108);
        lv_obj_t* date = lv_label_create(day_card);
        lv_label_set_text(date, day.date.data());
        lv_obj_set_width(date, kWeatherDayCardWidth);
        lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(date, lv_color_hex(0x94a3b8), 0);
        lv_obj_set_pos(date, 0, 144);
    }

    /* 新数据到达后回到第一天，避免沿用上一次预报的滚动偏移。 */
    lv_obj_update_layout(weather_panel_);
    lv_obj_scroll_to_x(weather_panel_, 0, LV_ANIM_OFF);
}

void WatchApplications::WeatherRefreshCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self != nullptr) self->StartWeatherFetch();
}
