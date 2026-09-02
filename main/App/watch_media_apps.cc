#include "watch_apps.h"

#include "application.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"

#include <esp_ae_rate_cvt.h>
#include <esp_audio_dec.h>
#include <esp_audio_dec_default.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "src/misc/cache/instance/lv_image_cache.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace {
constexpr char kTag[] = "watch_media";
constexpr size_t kMaximumJpegFrame = 512U * 1024U;
constexpr size_t kAudioInputBytes = 15U * 1024U;
constexpr size_t kAudioOutputBytes = 8U * 1024U;

const lv_font_t* GetMediaTextFont() {
    Display* display = Board::GetInstance().GetDisplay();
    if (display == nullptr || display->GetTheme() == nullptr) return LV_FONT_DEFAULT;
    auto* theme = static_cast<LvglTheme*>(display->GetTheme());
    if (theme->text_font() == nullptr || theme->text_font()->font() == nullptr) return LV_FONT_DEFAULT;
    return theme->text_font()->font();
}

lv_obj_t* CreateMediaButton(lv_obj_t* parent, const char* text, int width, int height, lv_color_t color) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, height / 2, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, GetMediaTextFont(), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

std::string LowerExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool IsSupportedFile(WatchApplications::AppId id, const std::string& name) {
    const std::string extension = LowerExtension(name);
    switch (id) {
        case WatchApplications::AppId::kPicture:
            return extension == ".jpg" || extension == ".jpeg" || extension == ".sjpg";
        case WatchApplications::AppId::kVideo:
            return extension == ".mjpeg" || extension == ".mjpg";
        case WatchApplications::AppId::kMusic:
            return extension == ".mp3";
        case WatchApplications::AppId::kComic:
            return extension == ".cmj";
        default:
            return false;
    }
}

const char* MediaTitle(WatchApplications::AppId id) {
    switch (id) {
        case WatchApplications::AppId::kPicture: return "图片";
        case WatchApplications::AppId::kVideo: return "视频";
        case WatchApplications::AppId::kMusic: return "音乐";
        case WatchApplications::AppId::kComic: return "漫画";
        default: return "媒体";
    }
}

const char* MediaRoot(WatchApplications::AppId id) {
    switch (id) {
        case WatchApplications::AppId::kPicture: return "/图片";
        case WatchApplications::AppId::kVideo: return "/视频";
        case WatchApplications::AppId::kMusic: return "/音乐";
        case WatchApplications::AppId::kComic: return "/漫画";
        default: return "/";
    }
}

void SetButtonText(lv_obj_t* label, const char* text) {
    if (label != nullptr) lv_label_set_text(label, text);
}
}  // namespace

void WatchApplications::CreatePicture() { CreateMediaList(AppId::kPicture); }
void WatchApplications::CreateVideo() { CreateMediaList(AppId::kVideo); }
void WatchApplications::CreateMusic() { CreateMediaList(AppId::kMusic); }
void WatchApplications::CreateComic() { CreateMediaList(AppId::kComic); }

void WatchApplications::CreateMediaList(AppId id) {
    CleanupMedia();
    lv_obj_clean(overlay_);
    active_app_ = id;
    media_mode_ = MediaMode::kList;
    media_root_ = MediaRoot(id);

    lv_obj_t* back = CreateMediaButton(overlay_, "返回", 76, 38, lv_color_hex(0x374151));
    lv_obj_set_pos(back, 12, 34);
    lv_obj_add_event_cb(back, MediaBackCallback, LV_EVENT_CLICKED, this);

    media_title_ = lv_label_create(overlay_);
    lv_label_set_text(media_title_, MediaTitle(id));
    lv_obj_set_style_text_font(media_title_, GetMediaTextFont(), 0);
    lv_obj_set_style_text_color(media_title_, lv_color_white(), 0);
    lv_obj_align(media_title_, LV_ALIGN_TOP_MID, 0, 17);

    media_list_ = lv_obj_create(overlay_);
    lv_obj_set_size(media_list_, 456, 228);
    lv_obj_set_pos(media_list_, 12, 82);
    lv_obj_set_flex_flow(media_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(media_list_, 8, 0);
    lv_obj_set_style_pad_row(media_list_, 7, 0);
    lv_obj_set_style_bg_color(media_list_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_width(media_list_, 0, 0);
    lv_obj_set_style_radius(media_list_, 14, 0);

    std::vector<WatchStorage::Entry> entries;
    const esp_err_t error = WatchStorage::Instance().ListDirectory(media_root_, &entries);
    if (error == ESP_OK) {
        for (auto& entry : entries) {
            if (!entry.is_directory && IsSupportedFile(id, entry.name)) media_entries_.push_back(std::move(entry));
        }
        std::sort(media_entries_.begin(), media_entries_.end(),
                  [](const auto& left, const auto& right) { return left.name < right.name; });
    }

    if (error != ESP_OK || media_entries_.empty()) {
        lv_obj_t* message = lv_label_create(media_list_);
        if (error != ESP_OK) {
            lv_label_set_text_fmt(message, "未找到可用的 MicroSD 卡\n%s", esp_err_to_name(error));
        } else {
            lv_label_set_text_fmt(message, "%s目录中没有支持的文件", media_root_.c_str());
        }
        lv_obj_set_width(message, 420);
        lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(message, lv_color_hex(0x94a3b8), 0);
        return;
    }

    for (size_t index = 0; index < media_entries_.size(); ++index) {
        lv_obj_t* button = CreateMediaButton(media_list_, media_entries_[index].name.c_str(), 420, 46,
                                             lv_color_hex(0x1f2937));
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(button, MediaItemCallback, LV_EVENT_CLICKED, this);
    }
}

void WatchApplications::OpenMediaEntry(size_t index) {
    if (index >= media_entries_.size()) return;
    media_index_ = index;
    switch (active_app_) {
        case AppId::kPicture: ShowPicture(index); break;
        case AppId::kVideo: ShowVideo(index); break;
        case AppId::kMusic: ShowMusic(index); break;
        case AppId::kComic: ShowComic(index); break;
        default: break;
    }
}

void WatchApplications::ShowPicture(size_t index) {
    if (index >= media_entries_.size()) return;
    lv_obj_clean(overlay_);
    media_mode_ = MediaMode::kPicture;
    media_index_ = index;
    media_path_ = media_root_ + "/" + media_entries_[index].name;
    std::string vfs_path;
    if (WatchStorage::Instance().GetVfsPath(media_path_, &vfs_path) != ESP_OK) return;
    media_lvgl_path_ = "A:" + vfs_path;

    lv_obj_t* back = CreateMediaButton(overlay_, "目录", 72, 36, lv_color_hex(0x374151));
    lv_obj_set_pos(back, 10, 34);
    lv_obj_add_event_cb(back, MediaBackCallback, LV_EVENT_CLICKED, this);
    media_title_ = lv_label_create(overlay_);
    lv_label_set_text(media_title_, media_entries_[index].name.c_str());
    lv_label_set_long_mode(media_title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(media_title_, 290);
    lv_obj_align(media_title_, LV_ALIGN_TOP_MID, 0, 16);

    media_image_ = lv_image_create(overlay_);
    lv_obj_set_size(media_image_, 456, 230);
    lv_obj_align(media_image_, LV_ALIGN_CENTER, 0, 5);
    lv_image_set_inner_align(media_image_, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_src(media_image_, media_lvgl_path_.c_str());

    lv_obj_t* previous = CreateMediaButton(overlay_, "上一张", 96, 38, lv_color_hex(0x334155));
    lv_obj_align(previous, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_obj_add_event_cb(previous, MediaPreviousCallback, LV_EVENT_CLICKED, this);
    lv_obj_t* carousel = CreateMediaButton(overlay_, "轮播", 96, 38, lv_color_hex(0x2563eb));
    lv_obj_align(carousel, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(carousel, PictureCarouselCallback, LV_EVENT_CLICKED, this);
    lv_obj_t* next = CreateMediaButton(overlay_, "下一张", 96, 38, lv_color_hex(0x334155));
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_add_event_cb(next, MediaNextCallback, LV_EVENT_CLICKED, this);
    Settings settings("watch", false);
    const int interval = std::clamp<int>(settings.GetInt("carousel_sec", 5), 1, 10);
    picture_next_us_ = esp_timer_get_time() + interval * 1000000LL;
}

void WatchApplications::ShowVideo(size_t index) {
    if (index >= media_entries_.size()) return;
    CleanupMedia();
    media_entries_.clear();
    std::vector<WatchStorage::Entry> all;
    if (WatchStorage::Instance().ListDirectory(MediaRoot(AppId::kVideo), &all) == ESP_OK) {
        for (auto& item : all) if (!item.is_directory && IsSupportedFile(AppId::kVideo, item.name)) media_entries_.push_back(std::move(item));
        std::sort(media_entries_.begin(), media_entries_.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    }
    if (index >= media_entries_.size()) return;
    media_index_ = index;
    media_root_ = MediaRoot(AppId::kVideo);
    media_path_ = media_root_ + "/" + media_entries_[index].name;
    std::string vfs_path;
    if (WatchStorage::Instance().GetVfsPath(media_path_, &vfs_path) != ESP_OK) return;
    video_task_path_ = vfs_path;
    struct stat info = {};
    if (stat(vfs_path.c_str(), &info) == 0) video_file_size_.store(static_cast<size_t>(info.st_size));

    lv_obj_clean(overlay_);
    media_mode_ = MediaMode::kVideo;
    video_playing_ = true;
    lv_obj_t* back = CreateMediaButton(overlay_, "目录", 72, 36, lv_color_hex(0x374151));
    lv_obj_set_pos(back, 10, 34);
    lv_obj_add_event_cb(back, MediaBackCallback, LV_EVENT_CLICKED, this);
    media_title_ = lv_label_create(overlay_);
    lv_label_set_text(media_title_, media_entries_[index].name.c_str());
    lv_label_set_long_mode(media_title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(media_title_, 300);
    lv_obj_align(media_title_, LV_ALIGN_TOP_MID, 0, 16);
    media_image_ = lv_image_create(overlay_);
    lv_obj_set_size(media_image_, 456, 226);
    lv_obj_align(media_image_, LV_ALIGN_CENTER, 0, 4);
    lv_image_set_inner_align(media_image_, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_style_bg_color(media_image_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(media_image_, LV_OPA_COVER, 0);
    lv_obj_t* play = CreateMediaButton(overlay_, "暂停", 90, 38, lv_color_hex(0x2563eb));
    lv_obj_align(play, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    media_play_button_label_ = lv_obj_get_child(play, 0);
    lv_obj_add_event_cb(play, VideoPlayCallback, LV_EVENT_CLICKED, this);
    media_progress_ = lv_bar_create(overlay_);
    lv_obj_set_size(media_progress_, 340, 10);
    lv_obj_align(media_progress_, LV_ALIGN_BOTTOM_RIGHT, -14, -22);
    lv_bar_set_range(media_progress_, 0, 1000);
    if (app_timer_ != nullptr) lv_timer_set_period(app_timer_, kUiRefreshPeriodMs);
    StartVideoTask();
}

void WatchApplications::ShowMusic(size_t index) {
    if (index >= media_entries_.size()) return;
    lv_obj_clean(overlay_);
    media_mode_ = MediaMode::kMusic;
    media_index_ = index;
    media_path_ = media_root_ + "/" + media_entries_[index].name;
    if (WatchStorage::Instance().GetVfsPath(media_path_, &music_task_path_) != ESP_OK) return;

    lv_obj_t* back = CreateMediaButton(overlay_, "目录", 72, 36, lv_color_hex(0x374151));
    lv_obj_set_pos(back, 10, 34);
    lv_obj_add_event_cb(back, MediaBackCallback, LV_EVENT_CLICKED, this);
    media_title_ = lv_label_create(overlay_);
    lv_label_set_text(media_title_, media_entries_[index].name.c_str());
    lv_label_set_long_mode(media_title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(media_title_, 330);
    lv_obj_align(media_title_, LV_ALIGN_TOP_MID, 20, 16);

    lv_obj_t* disc = lv_obj_create(overlay_);
    lv_obj_set_size(disc, 132, 132);
    lv_obj_align(disc, LV_ALIGN_CENTER, -105, -5);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x3b82f6), 0);
    lv_obj_set_style_border_width(disc, 8, 0);
    lv_obj_t* note = lv_label_create(disc);
    lv_label_set_text(note, "音乐");
    lv_obj_set_style_text_font(note, GetMediaTextFont(), 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0x93c5fd), 0);
    lv_obj_center(note);

    media_status_ = lv_label_create(overlay_);
    lv_label_set_text(media_status_, "准备播放");
    lv_obj_set_style_text_color(media_status_, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_pos(media_status_, 255, 88);
    media_progress_ = lv_bar_create(overlay_);
    lv_obj_set_size(media_progress_, 190, 10);
    lv_obj_set_pos(media_progress_, 255, 130);
    lv_bar_set_range(media_progress_, 0, 1000);
    lv_obj_t* play = CreateMediaButton(overlay_, "暂停", 104, 46, lv_color_hex(0x2563eb));
    lv_obj_set_pos(play, 297, 160);
    media_play_button_label_ = lv_obj_get_child(play, 0);
    lv_obj_add_event_cb(play, MusicPlayCallback, LV_EVENT_CLICKED, this);
    lv_obj_t* volume_text = lv_label_create(overlay_);
    lv_label_set_text(volume_text, "音量");
    lv_obj_set_pos(volume_text, 252, 231);
    media_volume_ = lv_slider_create(overlay_);
    lv_obj_set_size(media_volume_, 150, 16);
    lv_obj_set_pos(media_volume_, 302, 233);
    lv_slider_set_range(media_volume_, 0, 100);
    lv_slider_set_value(media_volume_, music_volume_percent_.load(), LV_ANIM_OFF);
    lv_obj_add_event_cb(media_volume_, MusicVolumeCallback, LV_EVENT_VALUE_CHANGED, this);
    StartMusicTask();
}

void WatchApplications::ShowComic(size_t index) {
    if (index >= media_entries_.size()) return;
    media_index_ = index;
    media_path_ = media_root_ + "/" + media_entries_[index].name;
    std::string vfs_path;
    if (WatchStorage::Instance().GetVfsPath(media_path_, &vfs_path) != ESP_OK) return;
    comic_file_ = std::fopen(vfs_path.c_str(), "rb");
    if (comic_file_ == nullptr) return;
    char magic[4] = {};
    uint32_t count = 0;
    if (std::fread(magic, 1, sizeof(magic), comic_file_) != sizeof(magic) ||
        std::memcmp(magic, "CMJB", 4) != 0 ||
        std::fread(&count, sizeof(count), 1, comic_file_) != 1 || count == 0 || count > 20000) {
        std::fclose(comic_file_);
        comic_file_ = nullptr;
        return;
    }
    comic_offsets_.resize(static_cast<size_t>(count) + 1U);
    if (std::fread(comic_offsets_.data(), sizeof(uint64_t), comic_offsets_.size(), comic_file_) != comic_offsets_.size()) {
        std::fclose(comic_file_);
        comic_file_ = nullptr;
        comic_offsets_.clear();
        return;
    }
    uint32_t saved_frame = 0;
    WatchStorage::Instance().LoadReadingOffset(media_path_, &saved_frame);
    comic_frame_ = std::min(saved_frame, count - 1U);

    lv_obj_clean(overlay_);
    media_mode_ = MediaMode::kComic;
    lv_obj_t* back = CreateMediaButton(overlay_, "目录", 72, 36, lv_color_hex(0x374151));
    lv_obj_set_pos(back, 10, 34);
    lv_obj_add_event_cb(back, MediaBackCallback, LV_EVENT_CLICKED, this);
    media_title_ = lv_label_create(overlay_);
    lv_obj_set_width(media_title_, 300);
    lv_obj_align(media_title_, LV_ALIGN_TOP_MID, 0, 16);
    media_image_ = lv_image_create(overlay_);
    lv_obj_set_size(media_image_, 456, 226);
    lv_obj_align(media_image_, LV_ALIGN_CENTER, 0, 4);
    lv_image_set_inner_align(media_image_, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_t* previous = CreateMediaButton(overlay_, "上一页", 96, 38, lv_color_hex(0x334155));
    lv_obj_align(previous, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_obj_add_event_cb(previous, MediaPreviousCallback, LV_EVENT_CLICKED, this);
    media_status_ = lv_label_create(overlay_);
    lv_obj_align(media_status_, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_t* next = CreateMediaButton(overlay_, "下一页", 96, 38, lv_color_hex(0x2563eb));
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_add_event_cb(next, MediaNextCallback, LV_EVENT_CLICKED, this);
    LoadComicFrame(comic_frame_);
}

void WatchApplications::ReturnToMediaList() {
    const AppId id = active_app_;
    CreateMediaList(id);
}

void WatchApplications::CleanupMedia() {
    picture_carousel_ = false;
    StopVideoTask();
    if (comic_file_ != nullptr) {
        WatchStorage::Instance().SaveReadingOffset(media_path_, comic_frame_);
        std::fclose(comic_file_);
        comic_file_ = nullptr;
    }
    StopMusicTask();
    if (video_image_.data != nullptr) lv_image_cache_drop(&video_image_);
    if (encoded_image_.data != nullptr) lv_image_cache_drop(&encoded_image_);
    encoded_frame_.clear();
    encoded_frame_.shrink_to_fit();
    encoded_image_ = {};
    comic_offsets_.clear();
    media_entries_.clear();
    media_root_.clear();
    media_path_.clear();
    media_lvgl_path_.clear();
    media_mode_ = MediaMode::kNone;
    media_list_ = media_image_ = media_title_ = media_status_ = nullptr;
    media_play_button_label_ = media_progress_ = media_volume_ = nullptr;
    video_file_size_.store(0);
    video_playing_ = false;
    video_task_path_.clear();
    video_image_ = {};
    video_display_slot_ = -1;
    // 离开视频后恢复普通应用约 60 FPS 的动态刷新，而不是旧版的 10 FPS。
    if (app_timer_ != nullptr) lv_timer_set_period(app_timer_, kUiRefreshPeriodMs);
}

void WatchApplications::UpdateMedia() {
    switch (media_mode_) {
        case MediaMode::kPicture: UpdatePicture(); break;
        case MediaMode::kVideo: UpdateVideo(); break;
        case MediaMode::kMusic: UpdateMusic(); break;
        case MediaMode::kComic: UpdateComic(); break;
        default: break;
    }
}

void WatchApplications::UpdatePicture() {
    if (!picture_carousel_ || media_entries_.empty() || esp_timer_get_time() < picture_next_us_) return;
    ShowPicture((media_index_ + 1U) % media_entries_.size());
}

bool WatchApplications::ReadNextJpegFrame(FILE* file, std::vector<uint8_t>* frame, size_t maximum_size,
                                           const std::atomic_bool* stop) {
    if (file == nullptr || frame == nullptr) return false;
    frame->clear();
    int previous = -1;
    int value = 0;
    bool started = false;
    while ((value = std::fgetc(file)) != EOF) {
        if (stop != nullptr && (frame->size() & 0x0fffU) == 0U && stop->load()) {
            frame->clear();
            return false;
        }
        if (!started) {
            if (previous == 0xff && value == 0xd8) {
                frame->push_back(0xff);
                frame->push_back(0xd8);
                started = true;
            }
            previous = value;
            continue;
        }
        frame->push_back(static_cast<uint8_t>(value));
        if (frame->size() > maximum_size) {
            frame->clear();
            return false;
        }
        if (previous == 0xff && value == 0xd9) return true;
        previous = value;
    }
    frame->clear();
    return false;
}

void WatchApplications::UpdateVideo() {
    if (media_image_ == nullptr) return;
    int selected = -1;
    uint32_t newest_generation = 0;
    for (size_t index = 0; index < video_frames_.size(); ++index) {
        if (video_frames_[index].state.load() == 1 &&
            (selected < 0 || video_frames_[index].generation > newest_generation)) {
            selected = static_cast<int>(index);
            newest_generation = video_frames_[index].generation;
        }
    }
    if (selected >= 0) {
        uint8_t expected = 1;
        if (video_frames_[selected].state.compare_exchange_strong(expected, 2)) {
            if (video_image_.data != nullptr) lv_image_cache_drop(&video_image_);
            if (video_display_slot_ >= 0) video_frames_[video_display_slot_].state.store(0);
            for (size_t index = 0; index < video_frames_.size(); ++index) {
                if (static_cast<int>(index) != selected && video_frames_[index].state.load() == 1) {
                    video_frames_[index].state.store(0);  // 丢弃已经过时的帧，优先保证触摸和画面低延迟
                }
            }
            video_display_slot_ = selected;
            video_image_ = {};
            video_image_.header.cf = LV_COLOR_FORMAT_RAW;
            video_image_.data_size = static_cast<uint32_t>(video_frames_[selected].data.size());
            video_image_.data = video_frames_[selected].data.data();
            lv_image_set_src(media_image_, &video_image_);
            lv_obj_invalidate(media_image_);
        }
    }
    if (media_progress_ != nullptr) lv_bar_set_value(media_progress_, video_progress_permille_.load(), LV_ANIM_OFF);
    if (video_error_.load()) {
        video_playing_ = false;
        SetButtonText(media_play_button_label_, "播放失败");
    }
}

/** 函数：启动 MJPEG 预读任务；参数：无；返回值：无；任务只访问文件和帧槽，不调用 LVGL */
void WatchApplications::StartVideoTask() {
    StopVideoTask();
    if (video_task_.load() != nullptr) {
        video_error_.store(true);
        return;
    }
    if (video_task_path_.empty()) return;
    for (auto& slot : video_frames_) {
        slot.state.store(0);
        slot.generation = 0;
        slot.data.clear();
        slot.data.reserve(64U * 1024U);
    }
    video_stop_.store(false);
    video_paused_.store(false);
    video_error_.store(false);
    video_progress_permille_.store(0);
    video_display_slot_ = -1;
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(VideoTaskEntry, "watch_mjpeg", 6144, this, 3, &handle, 0) != pdPASS) {
        video_error_.store(true);
        return;
    }
    video_task_.store(handle);
}

/** 函数：请求 MJPEG 任务退出并等待释放文件；参数：无；返回值：无 */
void WatchApplications::StopVideoTask() {
    TaskHandle_t handle = video_task_.load();
    if (handle == nullptr) return;
    video_stop_.store(true);
    for (int retry = 0; retry < 200 && video_task_.load() != nullptr; ++retry) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (video_task_.load() != nullptr) {
        ESP_LOGE(kTag, "MJPEG task did not stop within 2 seconds");
    }
}

/**
 * 函    数：MJPEG 后台预读任务
 * 参    数：parameter WatchApplications 实例
 * 返 回 值：无
 * 注意事项：任务把完整 JPEG 发布到三槽有界缓冲；槽满时等待，绝不修改 LVGL 对象。
 */
void WatchApplications::VideoTaskEntry(void* parameter) {
    auto* self = static_cast<WatchApplications*>(parameter);
    while (self->video_task_.load() == nullptr && !self->video_stop_.load()) {
        vTaskDelay(1);  // 等待创建方发布任务句柄，避免极短文件导致退出状态被覆盖
    }
    FILE* file = std::fopen(self->video_task_path_.c_str(), "rb");
    if (file == nullptr) {
        self->video_error_.store(true);
        self->video_task_.store(nullptr);
        vTaskDelete(nullptr);
        return;
    }
    uint32_t generation = 0;
    while (!self->video_stop_.load()) {
        if (self->video_paused_.load()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int free_slot = -1;
        for (size_t index = 0; index < self->video_frames_.size(); ++index) {
            uint8_t expected = 0;
            if (self->video_frames_[index].state.compare_exchange_strong(expected, 3)) {
                free_slot = static_cast<int>(index);  // 3 表示后台任务正在填充
                break;
            }
        }
        if (free_slot < 0) {
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
        }
        auto& slot = self->video_frames_[free_slot];
        bool decoded = self->ReadNextJpegFrame(file, &slot.data, kMaximumJpegFrame, &self->video_stop_);
        if (!decoded && !self->video_stop_.load()) {
            std::rewind(file);
            decoded = self->ReadNextJpegFrame(file, &slot.data, kMaximumJpegFrame, &self->video_stop_);
        }
        if (!decoded) {
            slot.state.store(0);
            if (!self->video_stop_.load()) self->video_error_.store(true);
            break;
        }
        slot.generation = ++generation;
        slot.state.store(1);
        const long position = std::ftell(file);
        const size_t file_size = self->video_file_size_.load();
        if (position >= 0 && file_size != 0) {
            self->video_progress_permille_.store(static_cast<uint32_t>(1000ULL * position / file_size));
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 上限约 50 帧，给 LVGL、触摸和小智任务保留 CPU 时间
    }
    std::fclose(file);
    self->video_task_.store(nullptr);
    vTaskDelete(nullptr);
}

bool WatchApplications::LoadComicFrame(uint32_t frame_index) {
    if (comic_file_ == nullptr || frame_index + 1U >= comic_offsets_.size()) return false;
    const uint64_t begin = comic_offsets_[frame_index];
    const uint64_t end = comic_offsets_[frame_index + 1U];
    if (end <= begin || end - begin > kMaximumJpegFrame) return false;
    if (encoded_image_.data != nullptr) lv_image_cache_drop(&encoded_image_);
    encoded_frame_.resize(static_cast<size_t>(end - begin));
    if (fseeko(comic_file_, static_cast<off_t>(begin), SEEK_SET) != 0 ||
        std::fread(encoded_frame_.data(), 1, encoded_frame_.size(), comic_file_) != encoded_frame_.size()) return false;
    encoded_image_ = {};
    encoded_image_.header.cf = LV_COLOR_FORMAT_RAW;
    encoded_image_.data_size = static_cast<uint32_t>(encoded_frame_.size());
    encoded_image_.data = encoded_frame_.data();
    lv_image_set_src(media_image_, &encoded_image_);
    lv_obj_invalidate(media_image_);
    comic_frame_ = frame_index;
    if (media_title_ != nullptr) lv_label_set_text(media_title_, media_entries_[media_index_].name.c_str());
    if (media_status_ != nullptr) {
        lv_label_set_text_fmt(media_status_, "%lu / %lu", static_cast<unsigned long>(frame_index + 1U),
                              static_cast<unsigned long>(comic_offsets_.size() - 1U));
    }
    WatchStorage::Instance().SaveReadingOffset(media_path_, comic_frame_);
    return true;
}

void WatchApplications::UpdateComic() {}

void WatchApplications::StartMusicTask() {
    StopMusicTask();
    music_stop_.store(false);
    music_paused_.store(false);
    music_progress_permille_.store(0);
    music_elapsed_seconds_.store(0);
    music_state_.store(MusicState::kLoading);
    Application::GetInstance().GetAudioService().Stop();
    music_audio_owned_ = true;
    TaskHandle_t handle = nullptr;
    const BaseType_t created = xTaskCreatePinnedToCore(MusicTaskEntry, "watch_mp3", 8192, this, 4, &handle, 0);
    if (created != pdPASS) {
        music_state_.store(MusicState::kError);
        music_audio_owned_ = false;
        Application::GetInstance().GetAudioService().Start();
        return;
    }
    music_task_.store(handle);
}

void WatchApplications::StopMusicTask() {
    TaskHandle_t handle = music_task_.load();
    if (handle != nullptr) {
        music_stop_.store(true);
        for (int retry = 0; retry < 100 && music_task_.load() != nullptr; ++retry) vTaskDelay(pdMS_TO_TICKS(10));
        handle = music_task_.exchange(nullptr);
        if (handle != nullptr) vTaskDelete(handle);
    }
    if (music_audio_owned_) {
        music_audio_owned_ = false;
        Application::GetInstance().GetAudioService().Start();
    }
    music_state_.store(MusicState::kIdle);
}

void WatchApplications::MusicTaskEntry(void* parameter) {
    auto* self = static_cast<WatchApplications*>(parameter);
    FILE* file = std::fopen(self->music_task_path_.c_str(), "rb");
    if (file == nullptr) {
        self->music_state_.store(MusicState::kError);
        self->music_task_.store(nullptr);
        vTaskDelete(nullptr);
        return;
    }
    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::rewind(file);

    esp_audio_dec_register_default();
    esp_audio_dec_cfg_t decoder_config = {};
    decoder_config.type = ESP_AUDIO_TYPE_MP3;
    esp_audio_dec_handle_t decoder = nullptr;
    if (esp_audio_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        std::fclose(file);
        self->music_state_.store(MusicState::kError);
        self->music_task_.store(nullptr);
        vTaskDelete(nullptr);
        return;
    }

    std::vector<uint8_t> input(kAudioInputBytes);
    std::vector<uint8_t> output(kAudioOutputBytes);
    std::vector<int16_t> mono;
    std::vector<int16_t> resampled;
    esp_ae_rate_cvt_handle_t rate_converter = nullptr;
    uint32_t converter_source_rate = 0;
    AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    codec->SetOutputVolume(self->music_volume_percent_.load());
    codec->EnableOutput(true);
    self->music_state_.store(MusicState::kPlaying);
    int64_t rendered_samples = 0;

    while (!self->music_stop_.load()) {
        if (self->music_paused_.load()) {
            self->music_state_.store(MusicState::kPaused);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        self->music_state_.store(MusicState::kPlaying);
        const size_t bytes_read = std::fread(input.data(), 1, input.size(), file);
        if (bytes_read == 0) break;
        esp_audio_dec_in_raw_t raw = {};
        raw.buffer = input.data();
        raw.len = static_cast<uint32_t>(bytes_read);
        while (raw.len != 0 && !self->music_stop_.load()) {
            esp_audio_dec_out_frame_t frame = {};
            frame.buffer = output.data();
            frame.len = static_cast<uint32_t>(output.size());
            esp_audio_err_t result = esp_audio_dec_process(decoder, &raw, &frame);
            if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                output.resize(frame.needed_size);
                continue;
            }
            if (result != ESP_AUDIO_ERR_OK || raw.consumed == 0) break;
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            if (frame.decoded_size == 0) continue;
            esp_audio_dec_info_t audio_info = {};
            if (esp_audio_dec_get_info(decoder, &audio_info) != ESP_AUDIO_ERR_OK ||
                audio_info.bits_per_sample != 16 || audio_info.channel == 0) continue;
            const int16_t* pcm = reinterpret_cast<const int16_t*>(frame.buffer);
            const size_t sample_values = frame.decoded_size / sizeof(int16_t);
            const size_t sample_points = sample_values / audio_info.channel;
            mono.resize(sample_points);
            if (audio_info.channel == 1) {
                std::copy_n(pcm, sample_points, mono.begin());
            } else {
                for (size_t i = 0; i < sample_points; ++i) {
                    int32_t sum = 0;
                    for (uint8_t channel = 0; channel < audio_info.channel; ++channel) sum += pcm[i * audio_info.channel + channel];
                    mono[i] = static_cast<int16_t>(sum / audio_info.channel);
                }
            }
            if (audio_info.sample_rate != static_cast<uint32_t>(codec->output_sample_rate())) {
                if (rate_converter == nullptr || converter_source_rate != audio_info.sample_rate) {
                    if (rate_converter != nullptr) esp_ae_rate_cvt_close(rate_converter);
                    esp_ae_rate_cvt_cfg_t config = {};
                    config.src_rate = audio_info.sample_rate;
                    config.dest_rate = codec->output_sample_rate();
                    config.channel = 1;
                    config.bits_per_sample = 16;
                    config.complexity = 1;
                    config.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
                    if (esp_ae_rate_cvt_open(&config, &rate_converter) != ESP_AE_ERR_OK) rate_converter = nullptr;
                    converter_source_rate = audio_info.sample_rate;
                }
                if (rate_converter == nullptr) continue;
                uint32_t output_points = 0;
                if (esp_ae_rate_cvt_get_max_out_sample_num(rate_converter, sample_points, &output_points) != ESP_AE_ERR_OK) continue;
                resampled.resize(output_points);
                if (esp_ae_rate_cvt_process(rate_converter, mono.data(), sample_points, resampled.data(), &output_points) != ESP_AE_ERR_OK) continue;
                resampled.resize(output_points);
                codec->OutputData(resampled);
                rendered_samples += output_points;
            } else {
                codec->OutputData(mono);
                rendered_samples += mono.size();
            }
            self->music_elapsed_seconds_.store(static_cast<uint32_t>(rendered_samples / codec->output_sample_rate()));
        }
        const long position = std::ftell(file);
        if (file_size > 0 && position >= 0) self->music_progress_permille_.store(static_cast<uint32_t>(1000LL * position / file_size));
    }
    if (rate_converter != nullptr) esp_ae_rate_cvt_close(rate_converter);
    esp_audio_dec_close(decoder);
    codec->EnableOutput(false);
    std::fclose(file);
    if (!self->music_stop_.load()) self->music_state_.store(MusicState::kFinished);
    self->music_task_.store(nullptr);
    vTaskDelete(nullptr);
}

void WatchApplications::UpdateMusic() {
    if (media_status_ == nullptr) return;
    const MusicState state = music_state_.load();
    const uint32_t seconds = music_elapsed_seconds_.load();
    const char* state_text = "停止";
    if (state == MusicState::kLoading) state_text = "正在载入";
    else if (state == MusicState::kPlaying) state_text = "正在播放";
    else if (state == MusicState::kPaused) state_text = "已暂停";
    else if (state == MusicState::kFinished) state_text = "播放完成";
    else if (state == MusicState::kError) state_text = "播放失败";
    lv_label_set_text_fmt(media_status_, "%s  %02lu:%02lu", state_text,
                          static_cast<unsigned long>(seconds / 60U), static_cast<unsigned long>(seconds % 60U));
    if (media_progress_ != nullptr) lv_bar_set_value(media_progress_, music_progress_permille_.load(), LV_ANIM_OFF);
    SetButtonText(media_play_button_label_, music_paused_.load() ? "播放" : "暂停");
    if (music_audio_owned_ && music_task_.load() == nullptr && state != MusicState::kLoading && state != MusicState::kPlaying && state != MusicState::kPaused) {
        music_audio_owned_ = false;
        Application::GetInstance().GetAudioService().Start();
    }
}

void WatchApplications::MediaItemCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->OpenMediaEntry(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
}

void WatchApplications::MediaBackCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->media_mode_ == MediaMode::kList) self->Close();
    else self->ReturnToMediaList();
}

void WatchApplications::MediaPreviousCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->media_mode_ == MediaMode::kPicture && !self->media_entries_.empty()) {
        self->ShowPicture((self->media_index_ + self->media_entries_.size() - 1U) % self->media_entries_.size());
    } else if (self->media_mode_ == MediaMode::kComic && self->comic_frame_ > 0) {
        self->LoadComicFrame(self->comic_frame_ - 1U);
    }
}

void WatchApplications::MediaNextCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->media_mode_ == MediaMode::kPicture && !self->media_entries_.empty()) {
        self->ShowPicture((self->media_index_ + 1U) % self->media_entries_.size());
    } else if (self->media_mode_ == MediaMode::kComic && self->comic_frame_ + 1U < self->comic_offsets_.size() - 1U) {
        self->LoadComicFrame(self->comic_frame_ + 1U);
    }
}

void WatchApplications::PictureCarouselCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->picture_carousel_ = !self->picture_carousel_;
    Settings settings("watch", false);
    const int interval = std::clamp<int>(settings.GetInt("carousel_sec", 5), 1, 10);
    self->picture_next_us_ = esp_timer_get_time() + interval * 1000000LL;
    lv_label_set_text(lv_obj_get_child(lv_event_get_target_obj(event), 0), self->picture_carousel_ ? "停止轮播" : "轮播");
}

void WatchApplications::VideoPlayCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->video_playing_ = !self->video_playing_;
    self->video_paused_.store(!self->video_playing_);
    SetButtonText(self->media_play_button_label_, self->video_playing_ ? "暂停" : "播放");
}

void WatchApplications::MusicPlayCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->music_task_.load() == nullptr) {
        self->StartMusicTask();
        return;
    }
    self->music_paused_.store(!self->music_paused_.load());
}

void WatchApplications::MusicVolumeCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    const uint8_t volume = static_cast<uint8_t>(lv_slider_get_value(lv_event_get_target_obj(event)));
    self->music_volume_percent_.store(volume);
    if (self->music_audio_owned_) Board::GetInstance().GetAudioCodec()->SetOutputVolume(volume);
}
