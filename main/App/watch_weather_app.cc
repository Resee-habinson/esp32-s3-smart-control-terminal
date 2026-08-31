#include "watch_apps.h"

#include "boards/common/board.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "watch_weather_assets.h"

#include <cJSON.h>
#include <esp_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr char kTag[] = "watch_weather";
constexpr size_t kMaximumResponseBytes = 32U * 1024U;

lv_image_dsc_t s_weather_images[WEATHER_FRAME_CNT];
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
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_RGB565;
        image.header.w = WEATHER_IMG_WIDTH;
        image.header.h = WEATHER_IMG_HEIGHT;
        image.header.stride = WEATHER_IMG_WIDTH * sizeof(uint16_t);
        image.data_size = WEATHER_IMG_WIDTH * WEATHER_IMG_HEIGHT * sizeof(uint16_t);
        image.data = reinterpret_cast<const uint8_t*>(watch_weather_pixels[index]);
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t* refresh = lv_button_create(overlay_);
    lv_obj_set_size(refresh, 54, 40);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x2563eb), 0);
    lv_obj_set_style_radius(refresh, 20, 0);
    lv_obj_set_style_border_width(refresh, 0, 0);
    lv_obj_t* refresh_icon = lv_label_create(refresh);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(refresh_icon, &lv_font_montserrat_14, 0);
    lv_obj_center(refresh_icon);
    lv_obj_add_event_cb(refresh, WeatherRefreshCallback, LV_EVENT_CLICKED, this);

    weather_panel_ = lv_obj_create(overlay_);
    lv_obj_set_size(weather_panel_, 460, 252);
    lv_obj_set_pos(weather_panel_, 10, 58);
    lv_obj_set_style_pad_all(weather_panel_, 0, 0);
    lv_obj_set_style_border_width(weather_panel_, 0, 0);
    lv_obj_set_style_radius(weather_panel_, 14, 0);
    lv_obj_set_style_bg_color(weather_panel_, lv_color_hex(0x0f172a), 0);
    lv_obj_set_scroll_dir(weather_panel_, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(weather_panel_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(weather_panel_, LV_SCROLLBAR_MODE_OFF);

    weather_status_ = lv_label_create(weather_panel_);
    lv_label_set_text(weather_status_, "正在获取天气…");
    lv_obj_set_style_text_font(weather_status_, GetWeatherFont(), 0);
    lv_obj_set_style_text_color(weather_status_, lv_color_hex(0xcbd5e1), 0);
    lv_obj_center(weather_status_);
    StartWeatherFetch();
}

void WatchApplications::StartWeatherFetch() {
    if (weather_task_.load() != nullptr) return;
    weather_cancel_.store(false);
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
    bool success = false;
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
        http->SetTimeout(8000);
        if (http->Open("GET", url) && http->GetStatusCode() == 200) {
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
                    WeatherDay& day = self->weather_days_[valid++];
                    std::memcpy(day.date.data(), date->valuestring + 5, 5);
                    day.date[5] = '\0';
                    day.code = static_cast<int16_t>(std::lround(code));
                    day.maximum_tenths = static_cast<int16_t>(std::lround(maximum * 10.0));
                    day.minimum_tenths = static_cast<int16_t>(std::lround(minimum * 10.0));
                }
                self->weather_day_count_ = valid;
                success = valid != 0 && !self->weather_cancel_.load();
                cJSON_Delete(root);
            }
        } else {
            http->Close();
        }
    }
    self->weather_state_.store(success ? WeatherState::kReady : WeatherState::kError);
    self->weather_task_.store(nullptr);
    ESP_LOGI(kTag, "Weather request finished: %s", success ? "ok" : "failed");
    vTaskDelete(nullptr);
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
    if (weather_panel_ == nullptr || weather_day_count_ == 0) return;
    lv_obj_clean(weather_panel_);
    weather_status_ = nullptr;
    lv_obj_t* content = lv_obj_create(weather_panel_);
    lv_obj_set_size(content, static_cast<int32_t>(weather_day_count_ * 100U), 236);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t index = 0; index < weather_day_count_; ++index) {
        const WeatherDay& day = weather_days_[index];
        const int x = static_cast<int>(index * 100U);
        lv_obj_t* image = lv_image_create(content);
        lv_image_set_src(image, &s_weather_images[WeatherImageIndex(day.code)]);
        lv_image_set_scale(image, 141);
        lv_obj_set_pos(image, x, -18);
        lv_obj_t* description = lv_label_create(content);
        lv_label_set_text(description, WeatherDescription(day.code));
        lv_obj_set_width(description, 100);
        lv_obj_set_style_text_font(description, GetWeatherFont(), 0);
        lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(description, lv_color_white(), 0);
        lv_obj_set_pos(description, x, 66);
        lv_obj_t* maximum = lv_label_create(content);
        lv_label_set_text_fmt(maximum, "最高 %.1f°", day.maximum_tenths / 10.0);
        lv_obj_set_width(maximum, 100);
        lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(maximum, lv_color_hex(0xfef08a), 0);
        lv_obj_set_pos(maximum, x, 116);
        lv_obj_t* minimum = lv_label_create(content);
        lv_label_set_text_fmt(minimum, "最低 %.1f°", day.minimum_tenths / 10.0);
        lv_obj_set_width(minimum, 100);
        lv_obj_set_style_text_align(minimum, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(minimum, lv_color_hex(0xbfdbfe), 0);
        lv_obj_set_pos(minimum, x, 154);
        lv_obj_t* date = lv_label_create(content);
        lv_label_set_text(date, day.date.data());
        lv_obj_set_width(date, 100);
        lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(date, lv_color_hex(0x94a3b8), 0);
        lv_obj_set_pos(date, x, 204);
    }
}

void WatchApplications::WeatherRefreshCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self != nullptr) self->StartWeatherFetch();
}
