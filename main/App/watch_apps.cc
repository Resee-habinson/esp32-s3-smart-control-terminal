#include "watch_apps.h"

#include "boards/common/board.h"
#include "boards/common/wifi_board.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stack>
#include <sys/time.h>
#include <vector>

LV_FONT_DECLARE(time_80);

namespace {
constexpr char kTag[] = "watch_apps";
constexpr int kWidth = 480;
constexpr int kHeight = 320;
constexpr int kGridSize = 4;
constexpr int kTileSize = 56;
constexpr int kTileGap = 6;
constexpr int kBoardSize = kGridSize * kTileSize + (kGridSize + 1) * kTileGap;
constexpr int64_t kUsPerSecond = 1000000;

/**
 * 函    数：判断文件是否为当前 LVGL 解码器可直接显示的壁纸
 * 参    数：name  SD 卡目录中的文件名
 * 返 回 值：true 表示 JPEG/SJPG 文件；false 表示目录或不支持的格式
 * 注意事项：当前固件启用了 TJPGD，PNG 未启用，避免错误地向用户展示无法解码的文件
 */
bool IsSupportedWallpaper(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".jpg" || extension == ".jpeg" || extension == ".sjpg";
}

/**
 * 函    数：生成 LVGL 下拉框使用的连续整数选项
 * 参    数：first 起始值；last 结束值，包含端点
 * 返 回 值：以换行符分隔的选项字符串
 * 注意事项：lv_dropdown_set_options 会复制字符串，因此返回的临时对象可安全释放
 */
std::string BuildNumberOptions(int first, int last) {
    std::string options;
    for (int value = first; value <= last; ++value) {
        if (!options.empty()) options.push_back('\n');
        options += std::to_string(value);
    }
    return options;
}

/** 函数：取得小智当前主题的中文字库；参数：无；返回值：有效 LVGL 字体指针 */
const lv_font_t* GetWatchTextFont() {
    Display* display = Board::GetInstance().GetDisplay();
    if (display == nullptr || display->GetTheme() == nullptr) return LV_FONT_DEFAULT;
    auto* theme = static_cast<LvglTheme*>(display->GetTheme());
    if (theme->text_font() == nullptr || theme->text_font()->font() == nullptr) return LV_FONT_DEFAULT;
    return theme->text_font()->font();
}

/** 函数：设置对象显隐；参数：obj 目标对象，hidden 是否隐藏；返回值：无 */
void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/** 函数：创建带文字的圆角按钮；参数：parent/text/color/width/height；返回值：按钮对象 */
lv_obj_t* CreateTextButton(lv_obj_t* parent, const char* text, lv_color_t color, int width, int height) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_radius(button, height / 2, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    /* LV_SYMBOL_* 位于 Montserrat 私用区；普通中英文继续使用主题动态字库。 */
    const bool is_lv_symbol = text != nullptr && static_cast<uint8_t>(text[0]) == 0xefU;
    lv_obj_set_style_text_font(label, is_lv_symbol ? &lv_font_montserrat_14 : GetWatchTextFont(), 0);
    lv_obj_center(label);
    return button;
}

lv_color_t TileColor(uint8_t value) {
    switch (value) {
        case 0: return lv_color_hex(0xcdc1b4);
        case 1: return lv_color_hex(0xeee4da);
        case 2: return lv_color_hex(0xede0c8);
        case 3: return lv_color_hex(0xf2b179);
        case 4: return lv_color_hex(0xf59563);
        case 5: return lv_color_hex(0xf67c5f);
        case 6: return lv_color_hex(0xf65e3b);
        default: return lv_color_hex(0xedcf72);
    }
}
}  // namespace

bool WatchApplications::Initialize(lv_obj_t* watch_screen) {
    if (watch_screen == nullptr || watch_screen_ != nullptr) return false;
    watch_screen_ = watch_screen;
    /* 独立 Screen 不会自动继承小智主界面的主题，必须显式绑定中文字库。 */
    lv_obj_set_style_text_font(watch_screen_, GetWatchTextFont(), 0);
    return true;
}

bool WatchApplications::OpenWeather() {
    return Open(static_cast<size_t>(AppId::kWeather));
}

bool WatchApplications::Open(size_t index) {
    if (watch_screen_ == nullptr || index >= static_cast<size_t>(AppId::kCount)) return false;
    Close();
    overlay_ = lv_obj_create(watch_screen_);
    if (overlay_ == nullptr) return false;
    lv_obj_set_size(overlay_, kWidth, kHeight);
    lv_obj_set_pos(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(overlay_, GetWatchTextFont(), 0);
    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    active_app_ = static_cast<AppId>(index);

    switch (active_app_) {
        case AppId::kClock: CreateClock(); break;
        case AppId::kPicture: CreatePicture(); break;
        case AppId::kNovel: CreateNovel(); break;
        case AppId::kVideo: CreateVideo(); break;
        case AppId::kMusic: CreateMusic(); break;
        case AppId::kGame: CreateGame(); break;
        case AppId::kCalculator: CreateCalculator(); break;
        case AppId::kStopwatch: CreateStopwatch(); break;
        case AppId::kCalendar: CreateCalendar(); break;
        case AppId::kComic: CreateComic(); break;
        case AppId::kFileManager: CreateFileManager(); break;
        case AppId::kSettings: CreateSettings(); break;
        case AppId::kWeather: CreateWeather(); break;
        case AppId::kCount:
            Close();
            return false;
    }
    app_timer_ = lv_timer_create(AppTimerCallback, 100, this);
    UpdateActiveApplication();
    lv_obj_move_foreground(overlay_);
    ESP_LOGI(kTag, "Opened watch app index=%u", static_cast<unsigned>(index));
    return true;
}

void WatchApplications::Close() {
    /* 小说只在离开阅读器时写一次 NVS，避免每次翻页造成不必要的 Flash 磨损。 */
    if (active_app_ == AppId::kNovel && !novel_path_.empty()) {
        WatchStorage::Instance().SaveReadingOffset(novel_path_, static_cast<uint32_t>(novel_page_start_));
    }
    CleanupMedia();
    if (app_timer_ != nullptr) {
        lv_timer_delete(app_timer_);
        app_timer_ = nullptr;
    }
    if (overlay_ != nullptr) {
        lv_obj_delete(overlay_);
        overlay_ = nullptr;
    }
    active_app_ = AppId::kCount;
    clock_wallpaper_ = nullptr;
    clock_time_label_ = clock_second_label_ = clock_date_label_ = nullptr;
    clock_wallpaper_lvgl_path_.clear();
    novel_entries_.clear();
    novel_path_.clear();
    novel_page_history_.clear();
    novel_list_ = novel_content_label_ = novel_progress_label_ = nullptr;
    novel_previous_button_ = novel_next_button_ = nullptr;
    novel_page_start_ = novel_next_offset_ = novel_file_size_ = 0;
    file_manager_entries_.clear();
    file_manager_title_ = file_manager_list_ = nullptr;
    file_details_dialog_ = file_delete_label_ = nullptr;
    file_manager_path_ = "/";
    file_manager_page_ = file_details_index_ = 0;
    file_delete_armed_ = false;
    game_score_label_ = nullptr;
    for (auto& row : game_tiles_) row.fill(nullptr);
    calculator_textarea_ = nullptr;
    stopwatch_ui_ = {};
    countdown_ui_ = {};
    calendar_title_label_ = calendar_days_container_ = nullptr;
    settings_list_ = settings_detail_ = settings_status_label_ = nullptr;
    settings_selector_ = settings_input_a_ = settings_input_b_ = nullptr;
    settings_time_inputs_.fill(nullptr);
    settings_wallpapers_.clear();
    settings_action_ = SettingsAction::kNone;
    weather_cancel_.store(true);
    weather_panel_ = weather_status_ = nullptr;
    weather_state_.store(WeatherState::kIdle);
    weather_rendered_state_ = WeatherState::kIdle;
}

void WatchApplications::AppTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<WatchApplications*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->UpdateActiveApplication();
}

void WatchApplications::UpdateActiveApplication() {
    switch (active_app_) {
        case AppId::kClock: UpdateClock(); break;
        case AppId::kStopwatch:
            UpdateStopwatch();
            UpdateCountdown();
            break;
        case AppId::kPicture:
        case AppId::kVideo:
        case AppId::kMusic:
        case AppId::kComic:
            UpdateMedia(); break;
        case AppId::kWeather:
            UpdateWeather(); break;
        default: break;
    }
}

void WatchApplications::CreateClock() {
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x050505), 0);

    /*
     * 原 Arduino 版本会把 SD 卡壁纸转换后写入 LittleFS。本机型的资源分区由小智固件管理，
     * 因此改为保存 SD 相对路径并由 LVGL TJPGD 直接解码，既保留壁纸功能，也不覆盖小智资源。
     */
    Settings settings("watch", false);
    const std::string wallpaper_path = settings.GetString("wallpaper_path");
    std::string vfs_path;
    if (!wallpaper_path.empty() && IsSupportedWallpaper(wallpaper_path) &&
        WatchStorage::Instance().GetVfsPath(wallpaper_path, &vfs_path) == ESP_OK) {
        clock_wallpaper_lvgl_path_ = "A:" + vfs_path;
        clock_wallpaper_ = lv_image_create(overlay_);
        lv_obj_set_size(clock_wallpaper_, kWidth, kHeight);
        lv_obj_set_pos(clock_wallpaper_, 0, 0);
        lv_image_set_inner_align(clock_wallpaper_, LV_IMAGE_ALIGN_COVER);
        lv_image_set_src(clock_wallpaper_, clock_wallpaper_lvgl_path_.c_str());
        lv_obj_set_style_image_opa(clock_wallpaper_, LV_OPA_70, 0);  // 压暗背景，保证白色时间文字可读
    }
    clock_time_label_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(clock_time_label_, &time_80, 0);
    lv_obj_set_style_text_color(clock_time_label_, lv_color_white(), 0);
    lv_obj_align(clock_time_label_, LV_ALIGN_CENTER, 0, -26);
    clock_second_label_ = lv_label_create(overlay_);
    lv_obj_set_style_text_color(clock_second_label_, lv_color_hex(0x55d6ff), 0);
    lv_obj_align(clock_second_label_, LV_ALIGN_CENTER, 0, 42);
    clock_date_label_ = lv_label_create(overlay_);
    lv_obj_set_style_text_color(clock_date_label_, lv_color_hex(0xb8b8b8), 0);
    lv_obj_align(clock_date_label_, LV_ALIGN_BOTTOM_MID, 0, -28);
}

void WatchApplications::UpdateClock() {
    if (clock_time_label_ == nullptr) return;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    static const char* const kWeekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    lv_label_set_text_fmt(clock_time_label_, "%02d:%02d", local.tm_hour, local.tm_min);
    lv_label_set_text_fmt(clock_second_label_, "%02d", local.tm_sec);
    lv_label_set_text_fmt(clock_date_label_, "%04d年%02d月%02d日  %s", local.tm_year + 1900,
                          local.tm_mon + 1, local.tm_mday, kWeekdays[local.tm_wday]);
}

void WatchApplications::CreateGame() {
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0xfaf8ef), 0);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay_, GameTouchCallback, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(overlay_, GameTouchCallback, LV_EVENT_RELEASED, this);
    lv_obj_t* title = lv_label_create(overlay_);
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x776e65), 0);
    lv_obj_set_pos(title, 24, 10);
    game_score_label_ = lv_label_create(overlay_);
    lv_obj_set_style_text_color(game_score_label_, lv_color_hex(0x776e65), 0);
    lv_obj_align(game_score_label_, LV_ALIGN_TOP_RIGHT, -24, 20);

    lv_obj_t* board = lv_obj_create(overlay_);
    lv_obj_set_size(board, kBoardSize, kBoardSize);
    lv_obj_align(board, LV_ALIGN_CENTER, 0, 23);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xbbada0), 0);
    lv_obj_set_style_pad_all(board, 0, 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_style_radius(board, 6, 0);
    lv_obj_remove_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(board, LV_OBJ_FLAG_CLICKABLE);
    game_grid_ = {};
    game_score_ = 0;
    for (int row = 0; row < kGridSize; ++row) {
        for (int col = 0; col < kGridSize; ++col) {
            lv_obj_t* tile = lv_obj_create(board);
            game_tiles_[row][col] = tile;
            lv_obj_set_size(tile, kTileSize, kTileSize);
            lv_obj_set_pos(tile, kTileGap + col * (kTileSize + kTileGap),
                           kTileGap + row * (kTileSize + kTileGap));
            lv_obj_set_style_pad_all(tile, 0, 0);
            lv_obj_set_style_border_width(tile, 0, 0);
            lv_obj_set_style_radius(tile, 4, 0);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* label = lv_label_create(tile);
            lv_obj_center(label);
        }
    }
    SpawnGameTile();
    SpawnGameTile();
    RefreshGame();
}

void WatchApplications::RefreshGame() {
    if (game_score_label_ == nullptr) return;
    lv_label_set_text_fmt(game_score_label_, "SCORE  %u", static_cast<unsigned>(game_score_));
    for (int row = 0; row < kGridSize; ++row) {
        for (int col = 0; col < kGridSize; ++col) {
            const uint8_t value = game_grid_[row][col];
            lv_obj_t* tile = game_tiles_[row][col];
            lv_obj_set_style_bg_color(tile, TileColor(value), 0);
            lv_obj_t* label = lv_obj_get_child(tile, 0);
            if (value == 0) lv_label_set_text(label, "");
            else lv_label_set_text_fmt(label, "%u", 1U << value);
            lv_obj_set_style_text_color(label, value <= 2 ? lv_color_hex(0x776e65) : lv_color_white(), 0);
        }
    }
}

void WatchApplications::SpawnGameTile() {
    std::array<int, 16> empty{};
    size_t count = 0;
    for (int row = 0; row < kGridSize; ++row) {
        for (int col = 0; col < kGridSize; ++col) {
            if (game_grid_[row][col] == 0) empty[count++] = row * kGridSize + col;
        }
    }
    if (count == 0) return;
    const int selected = empty[esp_random() % count];
    game_grid_[selected / kGridSize][selected % kGridSize] = (esp_random() % 10 == 0) ? 2 : 1;
}

void WatchApplications::MoveGame(int direction) {
    bool moved = false;
    for (int line = 0; line < kGridSize; ++line) {
        std::array<uint8_t, 4> before{};
        for (int pos = 0; pos < kGridSize; ++pos) {
            int row = (direction == 0) ? pos : (direction == 1 ? kGridSize - 1 - pos : line);
            int col = (direction == 2) ? pos : (direction == 3 ? kGridSize - 1 - pos : line);
            before[pos] = game_grid_[row][col];
        }
        std::vector<uint8_t> compact;
        for (uint8_t value : before) if (value != 0) compact.push_back(value);
        std::array<uint8_t, 4> after{};
        size_t out = 0;
        for (size_t i = 0; i < compact.size(); ++i) {
            if (i + 1 < compact.size() && compact[i] == compact[i + 1]) {
                after[out] = compact[i] + 1;
                game_score_ += 1U << after[out++];
                ++i;
            } else after[out++] = compact[i];
        }
        moved = moved || before != after;
        for (int pos = 0; pos < kGridSize; ++pos) {
            int row = (direction == 0) ? pos : (direction == 1 ? kGridSize - 1 - pos : line);
            int col = (direction == 2) ? pos : (direction == 3 ? kGridSize - 1 - pos : line);
            game_grid_[row][col] = after[pos];
        }
    }
    if (moved) {
        SpawnGameTile();
        RefreshGame();
    }
}

void WatchApplications::GameTouchCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    lv_indev_t* indev = lv_indev_active();
    if (self == nullptr || indev == nullptr) return;
    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        self->game_press_point_ = point;
        return;
    }
    const int dx = point.x - self->game_press_point_.x;
    const int dy = point.y - self->game_press_point_.y;
    if (LV_ABS(dx) < 24 && LV_ABS(dy) < 24) return;
    if (LV_ABS(dx) > LV_ABS(dy)) self->MoveGame(dx > 0 ? 3 : 2);
    else self->MoveGame(dy > 0 ? 1 : 0);
}


void WatchApplications::CreateCalculator() {
    calculator_expression_.clear();
    calculator_textarea_ = lv_textarea_create(overlay_);
    lv_obj_set_size(calculator_textarea_, 456, 68);
    lv_obj_align(calculator_textarea_, LV_ALIGN_TOP_MID, 0, 8);
    lv_textarea_set_one_line(calculator_textarea_, true);
    lv_obj_set_style_bg_color(calculator_textarea_, lv_color_black(), 0);
    lv_obj_set_style_text_color(calculator_textarea_, lv_color_white(), 0);
    lv_obj_set_style_text_align(calculator_textarea_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(calculator_textarea_, GetWatchTextFont(), 0);
    lv_obj_set_style_border_width(calculator_textarea_, 0, 0);
    lv_obj_remove_flag(calculator_textarea_, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    static const char* const kTokens[5][4] = {
        {"C", "÷", "×", "←"}, {"7", "8", "9", "-"}, {"4", "5", "6", "+"},
        {"1", "2", "3", "%"}, {"0", ".", "=", "="},
    };
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (row == 4 && col == 3) continue;
            const char* token = kTokens[row][col];
            const bool operation = std::strstr("+-×÷=%", token) != nullptr;
            const lv_color_t color = operation ? lv_color_hex(0xf59e0b) :
                                     (row == 0 ? lv_color_hex(0x505050) : lv_color_hex(0x2d2d2d));
            const int width = (row == 4 && col == 2) ? 220 : 106;
            lv_obj_t* button = CreateTextButton(overlay_, token, color, width, 40);
            lv_obj_set_pos(button, 12 + col * 114, 82 + row * 46);
            lv_obj_set_user_data(button, const_cast<char*>(token));
            lv_obj_add_event_cb(button, CalculatorButtonCallback, LV_EVENT_CLICKED, this);
        }
    }
}

void WatchApplications::CalculatorButtonCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    const char* token = static_cast<const char*>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (self != nullptr && token != nullptr) self->HandleCalculatorToken(token);
}

void WatchApplications::HandleCalculatorToken(const char* token) {
    if (std::strcmp(token, "C") == 0) calculator_expression_.clear();
    else if (std::strcmp(token, "←") == 0) {
        if (!calculator_expression_.empty()) {
            /* 从末尾回退到 UTF-8 起始字节，确保一次删除完整的 ×、÷ 等字符。 */
            size_t start = calculator_expression_.size() - 1;
            while (start > 0 &&
                   (static_cast<uint8_t>(calculator_expression_[start]) & 0xc0U) == 0x80U) --start;
            calculator_expression_.erase(start);
        }
    } else if (std::strcmp(token, "=") == 0) {
        EvaluateCalculator();
        return;
    } else if (calculator_expression_.size() < 40) calculator_expression_ += token;
    lv_textarea_set_text(calculator_textarea_, calculator_expression_.c_str());
}

void WatchApplications::EvaluateCalculator() {
    std::string clean = calculator_expression_;
    for (size_t pos = 0; (pos = clean.find("×", pos)) != std::string::npos;) clean.replace(pos, 2, "*");
    for (size_t pos = 0; (pos = clean.find("÷", pos)) != std::string::npos;) clean.replace(pos, 2, "/");
    std::stack<double> values;
    std::stack<char> ops;
    auto priority = [](char op) { return (op == '*' || op == '/') ? 2 : 1; };
    auto apply = [&values](char op) -> bool {
        if (values.size() < 2) return false;
        const double b = values.top(); values.pop();
        const double a = values.top(); values.pop();
        if (op == '/' && std::abs(b) < 1e-12) return false;
        values.push(op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : a / b);
        return true;
    };
    bool ok = !clean.empty();
    for (size_t i = 0; ok && i < clean.size();) {
        if ((clean[i] >= '0' && clean[i] <= '9') || clean[i] == '.') {
            char* end = nullptr;
            double value = std::strtod(clean.c_str() + i, &end);
            if (end == clean.c_str() + i) { ok = false; break; }
            i = static_cast<size_t>(end - clean.c_str());
            if (i < clean.size() && clean[i] == '%') { value /= 100.0; ++i; }
            values.push(value);
        } else if (std::strchr("+-*/", clean[i]) != nullptr) {
            while (!ops.empty() && priority(ops.top()) >= priority(clean[i])) {
                if (!apply(ops.top())) { ok = false; break; }
                ops.pop();
            }
            if (ok) ops.push(clean[i++]);
        } else ok = false;
    }
    while (ok && !ops.empty()) {
        if (!apply(ops.top())) ok = false;
        ops.pop();
    }
    if (!ok || values.size() != 1 || !std::isfinite(values.top())) {
        calculator_expression_ = "Error";
    } else {
        char result[48]{};
        const double value = values.top();
        if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e12)
            std::snprintf(result, sizeof(result), "%lld", static_cast<long long>(std::llround(value)));
        else std::snprintf(result, sizeof(result), "%.10g", value);
        calculator_expression_ = result;
    }
    lv_textarea_set_text(calculator_textarea_, calculator_expression_.c_str());
}

void WatchApplications::CreateStopwatch() {
    /* 原版为两张 240×280 横向翻页；此处仅把单页等比扩展到 480×320。 */
    lv_obj_t* scroll = lv_obj_create(overlay_);
    lv_obj_set_size(scroll, kWidth, kHeight);
    lv_obj_set_pos(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(scroll, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(scroll, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_t* left = lv_obj_create(scroll);
    lv_obj_t* right = lv_obj_create(scroll);
    for (lv_obj_t* page : {left, right}) {
        lv_obj_set_size(page, kWidth, kHeight);
        lv_obj_set_style_bg_color(page, lv_color_black(), 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_pos(right, kWidth, 0);

    lv_obj_t* title = lv_label_create(left);
    lv_label_set_text(title, "秒表");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    stopwatch_ui_.time_label = lv_label_create(left);
    lv_label_set_text(stopwatch_ui_.time_label, "00:00.00");
    lv_obj_set_style_text_font(stopwatch_ui_.time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(stopwatch_ui_.time_label, lv_color_white(), 0);
    lv_obj_align(stopwatch_ui_.time_label, LV_ALIGN_CENTER, 0, -20);

    stopwatch_ui_.start_button = CreateTextButton(left, LV_SYMBOL_PLAY, lv_color_hex(0x22c55e), 60, 60);
    stopwatch_ui_.pause_button = CreateTextButton(left, LV_SYMBOL_PAUSE, lv_color_hex(0x3b82f6), 60, 60);
    stopwatch_ui_.reset_button = CreateTextButton(left, LV_SYMBOL_REFRESH, lv_color_hex(0xef4444), 60, 60);
    lv_obj_set_style_radius(stopwatch_ui_.reset_button, 10, 0);
    lv_obj_align(stopwatch_ui_.start_button, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_align(stopwatch_ui_.pause_button, LV_ALIGN_BOTTOM_MID, -46, -20);
    lv_obj_align(stopwatch_ui_.reset_button, LV_ALIGN_BOTTOM_MID, 46, -20);
    lv_obj_add_event_cb(stopwatch_ui_.start_button, StopwatchStartCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(stopwatch_ui_.pause_button, StopwatchPauseCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(stopwatch_ui_.reset_button, StopwatchResetCallback, LV_EVENT_CLICKED, this);

    title = lv_label_create(right);
    lv_label_set_text(title, "倒计时");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    static const char kRollerOptions[] =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";
    countdown_ui_.minute_roller = lv_roller_create(right);
    countdown_ui_.second_roller = lv_roller_create(right);
    for (lv_obj_t* roller : {countdown_ui_.minute_roller, countdown_ui_.second_roller}) {
        lv_obj_set_width(roller, 92);
        lv_roller_set_options(roller, kRollerOptions, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(roller, 4);
        lv_obj_set_style_bg_opa(roller, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(roller, 0, LV_PART_MAIN);
        lv_obj_set_style_text_font(roller, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(roller, &lv_font_montserrat_48, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, lv_color_hex(0xfacc15), LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(roller, LV_OPA_TRANSP, LV_PART_SELECTED);
        lv_obj_set_scrollbar_mode(roller, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_align(countdown_ui_.minute_roller, LV_ALIGN_CENTER, -82, -30);
    lv_obj_align(countdown_ui_.second_roller, LV_ALIGN_CENTER, 82, -30);
    countdown_ui_.colon_label = lv_label_create(right);
    lv_label_set_text(countdown_ui_.colon_label, ":");
    lv_obj_set_style_text_font(countdown_ui_.colon_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(countdown_ui_.colon_label, lv_color_white(), 0);
    lv_obj_align(countdown_ui_.colon_label, LV_ALIGN_CENTER, 0, -35);

    countdown_ui_.arc = lv_arc_create(right);
    lv_obj_set_size(countdown_ui_.arc, 180, 180);
    lv_obj_align(countdown_ui_.arc, LV_ALIGN_CENTER, 0, -30);
    lv_arc_set_rotation(countdown_ui_.arc, 270);
    lv_arc_set_bg_angles(countdown_ui_.arc, 0, 360);
    lv_arc_set_value(countdown_ui_.arc, 100);
    lv_obj_remove_style(countdown_ui_.arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(countdown_ui_.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(countdown_ui_.arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(countdown_ui_.arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(countdown_ui_.arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(countdown_ui_.arc, lv_color_hex(0x22c55e), LV_PART_INDICATOR);
    countdown_ui_.time_label = lv_label_create(right);
    lv_obj_set_style_text_font(countdown_ui_.time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(countdown_ui_.time_label, lv_color_white(), 0);
    lv_obj_align(countdown_ui_.time_label, LV_ALIGN_CENTER, 0, -30);

    countdown_ui_.start_button = CreateTextButton(right, LV_SYMBOL_PLAY, lv_color_hex(0x22c55e), 60, 60);
    countdown_ui_.pause_button = CreateTextButton(right, LV_SYMBOL_PAUSE, lv_color_hex(0x3b82f6), 60, 60);
    countdown_ui_.reset_button = CreateTextButton(right, LV_SYMBOL_REFRESH, lv_color_hex(0xef4444), 60, 60);
    lv_obj_set_style_radius(countdown_ui_.reset_button, 10, 0);
    lv_obj_align(countdown_ui_.start_button, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_align(countdown_ui_.pause_button, LV_ALIGN_BOTTOM_MID, -46, -8);
    lv_obj_align(countdown_ui_.reset_button, LV_ALIGN_BOTTOM_MID, 46, -8);
    lv_obj_add_event_cb(countdown_ui_.start_button, CountdownStartCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(countdown_ui_.pause_button, CountdownPauseCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(countdown_ui_.reset_button, CountdownResetCallback, LV_EVENT_CLICKED, this);
    SetStopwatchButtons();
    SetCountdownButtons();
}

void WatchApplications::UpdateStopwatch() {
    if (stopwatch_ui_.time_label == nullptr) return;
    int64_t elapsed = stopwatch_elapsed_us_;
    if (stopwatch_state_ == StopwatchState::kRunning) elapsed += esp_timer_get_time() - stopwatch_started_us_;
    const uint64_t centiseconds = static_cast<uint64_t>(std::max<int64_t>(elapsed, 0) / 10000);
    lv_label_set_text_fmt(stopwatch_ui_.time_label, "%02llu:%02llu.%02llu",
                          (centiseconds / 6000) % 100, (centiseconds / 100) % 60, centiseconds % 100);
}

void WatchApplications::SetStopwatchButtons() {
    SetHidden(stopwatch_ui_.start_button, stopwatch_state_ != StopwatchState::kStopped);
    SetHidden(stopwatch_ui_.pause_button, stopwatch_state_ == StopwatchState::kStopped);
    SetHidden(stopwatch_ui_.reset_button, stopwatch_state_ == StopwatchState::kStopped);
    if (stopwatch_ui_.pause_button != nullptr)
        lv_label_set_text(lv_obj_get_child(stopwatch_ui_.pause_button, 0),
                          stopwatch_state_ == StopwatchState::kPaused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

void WatchApplications::StopwatchStartCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->stopwatch_elapsed_us_ = 0;
    self->stopwatch_started_us_ = esp_timer_get_time();
    self->stopwatch_state_ = StopwatchState::kRunning;
    self->SetStopwatchButtons();
}

void WatchApplications::StopwatchPauseCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->stopwatch_state_ == StopwatchState::kRunning) {
        self->stopwatch_elapsed_us_ += esp_timer_get_time() - self->stopwatch_started_us_;
        self->stopwatch_state_ = StopwatchState::kPaused;
    } else if (self->stopwatch_state_ == StopwatchState::kPaused) {
        self->stopwatch_started_us_ = esp_timer_get_time();
        self->stopwatch_state_ = StopwatchState::kRunning;
    }
    self->SetStopwatchButtons();
}

void WatchApplications::StopwatchResetCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->stopwatch_state_ = StopwatchState::kStopped;
    self->stopwatch_elapsed_us_ = 0;
    self->SetStopwatchButtons();
    self->UpdateStopwatch();
}

void WatchApplications::UpdateCountdown() {
    if (countdown_ui_.time_label == nullptr || countdown_state_ == CountdownState::kStopped) return;
    int64_t remaining = countdown_remaining_us_;
    if (countdown_state_ == CountdownState::kRunning) remaining = countdown_deadline_us_ - esp_timer_get_time();
    if (remaining <= 0) {
        countdown_state_ = CountdownState::kStopped;
        countdown_remaining_us_ = countdown_total_us_ = 0;
        SetCountdownButtons();
        return;
    }
    countdown_remaining_us_ = remaining;
    const int total_seconds = static_cast<int>((remaining + kUsPerSecond - 1) / kUsPerSecond);
    lv_label_set_text_fmt(countdown_ui_.time_label, "%02d:%02d", total_seconds / 60, total_seconds % 60);
    const int progress = countdown_total_us_ > 0 ? static_cast<int>(remaining * 100 / countdown_total_us_) : 0;
    lv_arc_set_value(countdown_ui_.arc, std::clamp(progress, 0, 100));
}

void WatchApplications::SetCountdownButtons() {
    const bool stopped = countdown_state_ == CountdownState::kStopped;
    SetHidden(countdown_ui_.minute_roller, !stopped);
    SetHidden(countdown_ui_.second_roller, !stopped);
    SetHidden(countdown_ui_.colon_label, !stopped);
    SetHidden(countdown_ui_.start_button, !stopped);
    SetHidden(countdown_ui_.time_label, stopped);
    SetHidden(countdown_ui_.arc, stopped);
    SetHidden(countdown_ui_.pause_button, stopped);
    SetHidden(countdown_ui_.reset_button, stopped);
    if (countdown_ui_.pause_button != nullptr)
        lv_label_set_text(lv_obj_get_child(countdown_ui_.pause_button, 0),
                          countdown_state_ == CountdownState::kPaused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

void WatchApplications::CountdownStartCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    const int seconds = lv_roller_get_selected(self->countdown_ui_.minute_roller) * 60 +
                        lv_roller_get_selected(self->countdown_ui_.second_roller);
    if (seconds <= 0) return;
    self->countdown_total_us_ = static_cast<int64_t>(seconds) * kUsPerSecond;
    self->countdown_remaining_us_ = self->countdown_total_us_;
    self->countdown_deadline_us_ = esp_timer_get_time() + self->countdown_total_us_;
    self->countdown_state_ = CountdownState::kRunning;
    self->SetCountdownButtons();
}

void WatchApplications::CountdownPauseCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->countdown_state_ == CountdownState::kRunning) {
        self->countdown_remaining_us_ = std::max<int64_t>(0, self->countdown_deadline_us_ - esp_timer_get_time());
        self->countdown_state_ = CountdownState::kPaused;
    } else if (self->countdown_state_ == CountdownState::kPaused) {
        self->countdown_deadline_us_ = esp_timer_get_time() + self->countdown_remaining_us_;
        self->countdown_state_ = CountdownState::kRunning;
    }
    self->SetCountdownButtons();
}

void WatchApplications::CountdownResetCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->countdown_state_ = CountdownState::kStopped;
    self->countdown_remaining_us_ = self->countdown_total_us_ = 0;
    self->SetCountdownButtons();
}
void WatchApplications::CreateCalendar() {
    /* 复用原工程的 LVGL 日历控件，仅针对 480×320 放大可视区域。 */
    lv_obj_t* calendar = lv_calendar_create(overlay_);
    lv_obj_set_size(calendar, 430, 294);
    lv_obj_align(calendar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(calendar, GetWatchTextFont(), 0);

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    calendar_year_ = local.tm_year + 1900;
    calendar_month_ = local.tm_mon + 1;
    if (calendar_year_ < 2020) {
        calendar_year_ = 2026;
        calendar_month_ = 1;
        local.tm_mday = 1;
    }
    lv_calendar_set_today_date(calendar, calendar_year_, calendar_month_, local.tm_mday);
    lv_calendar_set_month_shown(calendar, calendar_year_, calendar_month_);
#if LV_USE_CALENDAR_HEADER_ARROW
    lv_obj_t* header = lv_calendar_add_header_arrow(calendar);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
#elif LV_USE_CALENDAR_HEADER_DROPDOWN
    lv_calendar_add_header_dropdown(calendar);
#endif
    calendar_days_container_ = calendar;
}

void WatchApplications::ShiftCalendarMonth(int delta) {
    calendar_month_ += delta;
    if (calendar_month_ < 1) { calendar_month_ = 12; --calendar_year_; }
    if (calendar_month_ > 12) { calendar_month_ = 1; ++calendar_year_; }
    if (calendar_days_container_ != nullptr)
        lv_calendar_set_month_shown(calendar_days_container_, calendar_year_, calendar_month_);
}

void WatchApplications::CalendarPreviousCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self != nullptr) self->ShiftCalendarMonth(-1);
}

void WatchApplications::CalendarNextCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self != nullptr) self->ShiftCalendarMonth(1);
}

void WatchApplications::RefreshCalendar() {
    if (calendar_days_container_ != nullptr)
        lv_calendar_set_month_shown(calendar_days_container_, calendar_year_, calendar_month_);
}

void WatchApplications::CreateSettings() {
    /* 菜单名称与顺序严格保留原 Arduino 工程。 */
    static const char* const kItems[] = {
        "时间同步", "WiFi", "壁纸", "电池校准", "自动轮播间隔", "重新扫描SD卡", "天气设置",
    };
    settings_list_ = lv_list_create(overlay_);
    lv_obj_set_size(settings_list_, 430, 294);
    lv_obj_center(settings_list_);
    lv_obj_set_style_text_font(settings_list_, GetWatchTextFont(), 0);
    for (const char* item_text : kItems) {
        lv_obj_t* button = lv_list_add_button(settings_list_, nullptr, item_text);
        lv_obj_set_height(button, 50);
        lv_obj_set_style_text_font(button, GetWatchTextFont(), 0);
        lv_obj_add_event_cb(button, SettingsItemCallback, LV_EVENT_CLICKED, this);
    }
}

void WatchApplications::ShowSettingsDetail(const char* item_text) {
    if (item_text == nullptr || settings_detail_ != nullptr) return;
    lv_obj_add_flag(settings_list_, LV_OBJ_FLAG_HIDDEN);
    settings_detail_ = lv_obj_create(overlay_);
    lv_obj_set_size(settings_detail_, kWidth, kHeight);
    lv_obj_set_pos(settings_detail_, 0, 0);
    lv_obj_set_style_bg_color(settings_detail_, lv_color_black(), 0);
    lv_obj_set_style_border_width(settings_detail_, 0, 0);
    lv_obj_set_style_radius(settings_detail_, 0, 0);
    lv_obj_set_style_text_font(settings_detail_, GetWatchTextFont(), 0);
    lv_obj_remove_flag(settings_detail_, LV_OBJ_FLAG_SCROLLABLE);
    settings_action_ = SettingsAction::kNone;
    settings_selector_ = settings_input_a_ = settings_input_b_ = nullptr;
    settings_time_inputs_.fill(nullptr);

    lv_obj_t* back = CreateTextButton(settings_detail_, LV_SYMBOL_LEFT, lv_color_hex(0x303030), 54, 42);
    lv_obj_set_pos(back, 14, 12);
    lv_obj_add_event_cb(back, SettingsBackCallback, LV_EVENT_CLICKED, this);
    lv_obj_t* title = lv_label_create(settings_detail_);
    lv_label_set_text(title, item_text);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);
    settings_status_label_ = lv_label_create(settings_detail_);
    lv_obj_set_width(settings_status_label_, 410);
    lv_obj_set_style_text_align(settings_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(settings_status_label_, lv_color_hex(0xd1d5db), 0);
    lv_obj_align(settings_status_label_, LV_ALIGN_CENTER, 0, 6);

    if (std::strcmp(item_text, "时间同步") == 0) {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        lv_label_set_text_fmt(settings_status_label_, "小智联网时自动校时；也可在下方手动设置\n当前：%04d-%02d-%02d  %02d:%02d:%02d",
                              local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                              local.tm_hour, local.tm_min, local.tm_sec);
        lv_obj_align(settings_status_label_, LV_ALIGN_TOP_MID, 0, 58);

        static const char* const kCaptions[] = {"年", "月", "日", "时", "分"};
        static const int kX[] = {26, 126, 206, 286, 366};
        static const int kWidths[] = {88, 68, 68, 68, 68};
        const std::string option_sets[] = {
            BuildNumberOptions(2020, 2030), BuildNumberOptions(1, 12), BuildNumberOptions(1, 31),
            BuildNumberOptions(0, 23), BuildNumberOptions(0, 59),
        };
        const int selected[] = {
            std::clamp(local.tm_year + 1900, 2020, 2030) - 2020,
            std::clamp(local.tm_mon, 0, 11), std::clamp(local.tm_mday - 1, 0, 30),
            std::clamp(local.tm_hour, 0, 23), std::clamp(local.tm_min, 0, 59),
        };
        for (size_t index = 0; index < settings_time_inputs_.size(); ++index) {
            lv_obj_t* caption = lv_label_create(settings_detail_);
            lv_label_set_text(caption, kCaptions[index]);
            lv_obj_set_pos(caption, kX[index] + kWidths[index] / 2 - 8, 96);
            settings_time_inputs_[index] = lv_dropdown_create(settings_detail_);
            lv_dropdown_set_options(settings_time_inputs_[index], option_sets[index].c_str());
            lv_dropdown_set_selected(settings_time_inputs_[index], selected[index]);
            lv_dropdown_set_symbol(settings_time_inputs_[index], nullptr);
            lv_obj_set_size(settings_time_inputs_[index], kWidths[index], 44);
            lv_obj_set_pos(settings_time_inputs_[index], kX[index], 120);
            lv_obj_set_style_text_font(settings_time_inputs_[index], GetWatchTextFont(), 0);
            lv_obj_set_style_text_font(lv_dropdown_get_list(settings_time_inputs_[index]), GetWatchTextFont(), 0);
        }
        settings_action_ = SettingsAction::kSetManualTime;
        lv_obj_t* action = CreateTextButton(settings_detail_, "设置时间", lv_color_hex(0x16a34a), 160, 46);
        lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -34);
        lv_obj_add_event_cb(action, SettingsActionCallback, LV_EVENT_CLICKED, this);
    } else if (std::strcmp(item_text, "WiFi") == 0) {
        lv_label_set_text(settings_status_label_, "WiFi 由小智系统统一管理\n点击下方按钮可进入配网模式");
        settings_action_ = SettingsAction::kWifiConfig;
        lv_obj_t* action = CreateTextButton(settings_detail_, "进入配网", lv_color_hex(0x2563eb), 180, 48);
        lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -38);
        lv_obj_add_event_cb(action, SettingsActionCallback, LV_EVENT_CLICKED, this);
    } else if (std::strcmp(item_text, "壁纸") == 0) {
        settings_wallpapers_.clear();
        std::vector<WatchStorage::Entry> entries;
        const esp_err_t error = WatchStorage::Instance().ListDirectory("/壁纸", &entries);
        if (error != ESP_OK) {
            lv_label_set_text_fmt(settings_status_label_, "无法读取 MicroSD 卡 /壁纸 目录\n%s", esp_err_to_name(error));
        } else {
            for (auto& entry : entries) {
                if (!entry.is_directory && IsSupportedWallpaper(entry.name)) {
                    settings_wallpapers_.push_back(std::move(entry));
                }
            }
            std::sort(settings_wallpapers_.begin(), settings_wallpapers_.end(),
                      [](const auto& left, const auto& right) { return left.name < right.name; });

            std::string options = "默认黑色";
            for (const auto& entry : settings_wallpapers_) options += "\n" + entry.name;
            settings_selector_ = lv_dropdown_create(settings_detail_);
            lv_dropdown_set_options(settings_selector_, options.c_str());
            lv_obj_set_width(settings_selector_, 320);
            lv_obj_align(settings_selector_, LV_ALIGN_CENTER, 0, -8);
            lv_obj_set_style_text_font(settings_selector_, GetWatchTextFont(), 0);
            lv_obj_set_style_text_font(lv_dropdown_get_list(settings_selector_), GetWatchTextFont(), 0);

            Settings settings("watch", false);
            const std::string selected_path = settings.GetString("wallpaper_path");
            for (size_t index = 0; index < settings_wallpapers_.size(); ++index) {
                if (selected_path == "/壁纸/" + settings_wallpapers_[index].name) {
                    lv_dropdown_set_selected(settings_selector_, static_cast<uint32_t>(index + 1));
                    break;
                }
            }
            lv_label_set_text(settings_status_label_, settings_wallpapers_.empty()
                                  ? "未找到 JPEG 壁纸\n请放入 MicroSD 卡 /壁纸 目录"
                                  : "选择壁纸后点击应用\n进入时钟应用即可查看效果");
            lv_obj_align(settings_status_label_, LV_ALIGN_TOP_MID, 0, 68);
            settings_action_ = SettingsAction::kSaveWallpaper;
            lv_obj_t* action = CreateTextButton(settings_detail_, "应用壁纸", lv_color_hex(0x2563eb), 170, 46);
            lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -34);
            lv_obj_add_event_cb(action, SettingsActionCallback, LV_EVENT_CLICKED, this);
        }
    } else if (std::strcmp(item_text, "电池校准") == 0) {
        int level = 0;
        bool charging = false;
        bool discharging = false;
        if (Board::GetInstance().GetBatteryLevel(level, charging, discharging))
            lv_label_set_text_fmt(settings_status_label_, "当前电量：%d%%\n%s", level, charging ? "正在充电" : "未在充电");
        else
            lv_label_set_text(settings_status_label_, "当前硬件未提供电池电量采样接口");
    } else if (std::strcmp(item_text, "自动轮播间隔") == 0) {
        lv_label_set_text(settings_status_label_, "设置图片应用自动轮播间隔");
        lv_obj_align(settings_status_label_, LV_ALIGN_TOP_MID, 0, 76);
        settings_selector_ = lv_dropdown_create(settings_detail_);
        lv_dropdown_set_options(settings_selector_, "1秒\n2秒\n3秒\n4秒\n5秒\n6秒\n7秒\n8秒\n9秒\n10秒");
        lv_obj_set_width(settings_selector_, 150);
        lv_obj_align(settings_selector_, LV_ALIGN_CENTER, 0, -4);
        lv_obj_set_style_text_font(settings_selector_, GetWatchTextFont(), 0);
        Settings settings("watch", false);
        const int interval = std::clamp<int>(settings.GetInt("carousel_sec", 5), 1, 10);
        lv_dropdown_set_selected(settings_selector_, interval - 1);
        settings_action_ = SettingsAction::kSaveCarousel;
        lv_obj_t* action = CreateTextButton(settings_detail_, "保存", lv_color_hex(0x2563eb), 150, 46);
        lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -34);
        lv_obj_add_event_cb(action, SettingsActionCallback, LV_EVENT_CLICKED, this);
    } else if (std::strcmp(item_text, "重新扫描SD卡") == 0) {
        lv_label_set_text(settings_status_label_, "插入 MicroSD 卡后点击下方按钮重新挂载");
        settings_action_ = SettingsAction::kRescanSd;
        lv_obj_t* action = CreateTextButton(settings_detail_, "重新扫描", lv_color_hex(0x2563eb), 180, 48);
        lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -38);
        lv_obj_add_event_cb(action, SettingsActionCallback, LV_EVENT_CLICKED, this);
    } else if (std::strcmp(item_text, "天气设置") == 0) {
        lv_label_set_text(settings_status_label_, "经度 / 纬度");
        lv_obj_align(settings_status_label_, LV_ALIGN_TOP_MID, 0, 67);
        Settings settings("watch", false);
        settings_input_a_ = lv_spinbox_create(settings_detail_);
        lv_spinbox_set_range(settings_input_a_, -1800000, 1800000);
        lv_spinbox_set_digit_format(settings_input_a_, 7, 3);
        lv_spinbox_set_value(settings_input_a_, settings.GetInt("weather_lon_e4", 1214700));
        lv_obj_set_size(settings_input_a_, 145, 42);
        lv_obj_set_pos(settings_input_a_, 54, 112);
        settings_input_b_ = lv_spinbox_create(settings_detail_);
        lv_spinbox_set_range(settings_input_b_, -900000, 900000);
        lv_spinbox_set_digit_format(settings_input_b_, 6, 2);
        lv_spinbox_set_value(settings_input_b_, settings.GetInt("weather_lat_e4", 312300));
        lv_obj_set_size(settings_input_b_, 145, 42);
        lv_obj_set_pos(settings_input_b_, 282, 112);
        for (lv_obj_t* input : {settings_input_a_, settings_input_b_}) {
            lv_spinbox_set_step(input, 100);
            lv_obj_set_style_text_font(input, GetWatchTextFont(), 0);
            lv_obj_set_style_text_align(input, LV_TEXT_ALIGN_CENTER, 0);
        }
        const int8_t adjustments[] = {-1, 1, -2, 2};
        const int positions[] = {18, 203, 246, 431};
        const char* symbols[] = {LV_SYMBOL_MINUS, LV_SYMBOL_PLUS, LV_SYMBOL_MINUS, LV_SYMBOL_PLUS};
        for (size_t index = 0; index < 4; ++index) {
            lv_obj_t* adjust = CreateTextButton(settings_detail_, symbols[index], lv_color_hex(0x374151), 42, 42);
            lv_obj_set_pos(adjust, positions[index], 112);
            lv_obj_set_user_data(adjust, reinterpret_cast<void*>(static_cast<intptr_t>(adjustments[index])));
            lv_obj_add_event_cb(adjust, SettingsAdjustCallback, LV_EVENT_CLICKED, this);
        }
        settings_action_ = SettingsAction::kSaveWeather;
        lv_obj_t* action = CreateTextButton(settings_detail_, "保存位置", lv_color_hex(0x2563eb), 160, 46);
        lv_obj_align(action, LV_ALIGN_BOTTOM_MID, 0, -34);
        lv_obj_add_event_cb(action, SettingsActionCallback, LV_EVENT_CLICKED, this);
    }
}

void WatchApplications::SettingsItemCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr || self->settings_list_ == nullptr) return;
    const char* text = lv_list_get_button_text(self->settings_list_, lv_event_get_target_obj(event));
    self->ShowSettingsDetail(text);
}

void WatchApplications::SettingsBackCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr || self->settings_detail_ == nullptr) return;
    lv_obj_delete(self->settings_detail_);
    self->settings_detail_ = nullptr;
    self->settings_status_label_ = nullptr;
    self->settings_selector_ = self->settings_input_a_ = self->settings_input_b_ = nullptr;
    self->settings_time_inputs_.fill(nullptr);
    self->settings_wallpapers_.clear();
    self->settings_action_ = SettingsAction::kNone;
    lv_obj_remove_flag(self->settings_list_, LV_OBJ_FLAG_HIDDEN);
}

void WatchApplications::SettingsActionCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr || self->settings_status_label_ == nullptr) return;
    switch (self->settings_action_) {
        case SettingsAction::kWifiConfig:
            lv_label_set_text(self->settings_status_label_, "正在进入配网模式…");
            static_cast<WifiBoard&>(Board::GetInstance()).EnterWifiConfigMode();
            break;
        case SettingsAction::kSaveWallpaper: {
            if (self->settings_selector_ == nullptr) break;
            const uint32_t selected = lv_dropdown_get_selected(self->settings_selector_);
            Settings settings("watch", true);
            if (selected == 0) {
                settings.SetString("wallpaper_path", "");
                lv_label_set_text(self->settings_status_label_, "已恢复默认黑色背景");
            } else if (selected - 1 < self->settings_wallpapers_.size()) {
                const std::string path = "/壁纸/" + self->settings_wallpapers_[selected - 1].name;
                settings.SetString("wallpaper_path", path);
                lv_label_set_text(self->settings_status_label_, "壁纸已保存\n进入时钟应用即可查看效果");
            }
            break;
        }
        case SettingsAction::kSetManualTime: {
            for (lv_obj_t* input : self->settings_time_inputs_) {
                if (input == nullptr) return;
            }
            const int year = 2020 + static_cast<int>(lv_dropdown_get_selected(self->settings_time_inputs_[0]));
            const int month = 1 + static_cast<int>(lv_dropdown_get_selected(self->settings_time_inputs_[1]));
            const int day = 1 + static_cast<int>(lv_dropdown_get_selected(self->settings_time_inputs_[2]));
            const int hour = static_cast<int>(lv_dropdown_get_selected(self->settings_time_inputs_[3]));
            const int minute = static_cast<int>(lv_dropdown_get_selected(self->settings_time_inputs_[4]));

            std::tm requested{};
            requested.tm_year = year - 1900;
            requested.tm_mon = month - 1;
            requested.tm_mday = day;
            requested.tm_hour = hour;
            requested.tm_min = minute;
            requested.tm_isdst = -1;
            const std::time_t timestamp = std::mktime(&requested);
            if (timestamp < 0 || requested.tm_year != year - 1900 || requested.tm_mon != month - 1 ||
                requested.tm_mday != day || requested.tm_hour != hour || requested.tm_min != minute) {
                lv_label_set_text(self->settings_status_label_, "日期无效，请检查月份和日期");
                break;
            }
            const timeval system_time{.tv_sec = timestamp, .tv_usec = 0};
            if (settimeofday(&system_time, nullptr) != 0) {
                lv_label_set_text(self->settings_status_label_, "设置失败，请稍后重试");
                ESP_LOGE(kTag, "settimeofday failed");
                break;
            }
            lv_label_set_text_fmt(self->settings_status_label_, "时间设置成功\n%04d-%02d-%02d  %02d:%02d",
                                  year, month, day, hour, minute);
            break;
        }
        case SettingsAction::kRescanSd: {
            lv_label_set_text(self->settings_status_label_, "正在扫描 MicroSD 卡...");
            lv_refr_now(nullptr);
            const esp_err_t error = WatchStorage::Instance().EnsureMounted();
            if (error == ESP_OK) lv_label_set_text(self->settings_status_label_, "MicroSD 卡已挂载");
            else lv_label_set_text_fmt(self->settings_status_label_, "未检测到可用 MicroSD 卡\n%s", esp_err_to_name(error));
            break;
        }
        case SettingsAction::kSaveCarousel: {
            if (self->settings_selector_ == nullptr) break;
            const int interval = lv_dropdown_get_selected(self->settings_selector_) + 1;
            Settings settings("watch", true);
            settings.SetInt("carousel_sec", interval);
            lv_label_set_text_fmt(self->settings_status_label_, "已保存：%d 秒", interval);
            break;
        }
        case SettingsAction::kSaveWeather: {
            if (self->settings_input_a_ == nullptr || self->settings_input_b_ == nullptr) break;
            Settings settings("watch", true);
            settings.SetInt("weather_lon_e4", lv_spinbox_get_value(self->settings_input_a_));
            settings.SetInt("weather_lat_e4", lv_spinbox_get_value(self->settings_input_b_));
            lv_label_set_text(self->settings_status_label_, "天气位置已保存");
            break;
        }
        case SettingsAction::kNone:
            break;
    }
}

void WatchApplications::SettingsAdjustCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    const intptr_t adjustment = reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    lv_obj_t* spinbox = std::abs(adjustment) == 1 ? self->settings_input_a_ : self->settings_input_b_;
    if (spinbox == nullptr) return;
    if (adjustment < 0) lv_spinbox_decrement(spinbox);
    else lv_spinbox_increment(spinbox);
}
