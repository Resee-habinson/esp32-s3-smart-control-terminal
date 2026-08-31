#include "watch_apps.h"

#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kTag[] = "watch_storage_apps";
constexpr size_t kNovelPageBytes = 1024;
constexpr size_t kFilesPerPage = 30;

/** 函数：创建统一样式文字按钮；参数：父对象、文字、宽、高、颜色；返回值：按钮对象 */
lv_obj_t* CreateStorageButton(lv_obj_t* parent, const char* text, int width, int height, lv_color_t color) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width - 24);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

/** 函数：判断扩展名（忽略大小写）；参数：文件名、扩展名；返回值：是否匹配 */
bool HasExtension(const std::string& name, const char* extension) {
    const size_t extension_length = std::strlen(extension);
    if (name.size() < extension_length) return false;
    return std::equal(name.end() - extension_length, name.end(), extension,
                      [](char left, char right) {
                          return std::tolower(static_cast<unsigned char>(left)) ==
                                 std::tolower(static_cast<unsigned char>(right));
                      });
}

/** 函数：取得 UTF-8 首字节声明的字符长度；参数：首字节；返回值：1～4 */
size_t Utf8Length(unsigned char byte) {
    if ((byte & 0x80U) == 0) return 1;
    if ((byte & 0xe0U) == 0xc0U) return 2;
    if ((byte & 0xf0U) == 0xe0U) return 3;
    if ((byte & 0xf8U) == 0xf0U) return 4;
    return 1;
}

/** 函数：创建页面标题；参数：父对象、标题；返回值：标题标签 */
lv_obj_t* CreatePageTitle(lv_obj_t* parent, const char* title) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 16);
    return label;
}

/** 函数：在页面中央显示存储错误；参数：父对象、说明、错误码；返回值：无 */
void ShowStorageError(lv_obj_t* parent, const char* message, esp_err_t error) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text_fmt(label, "%s\n%s\n\n请检查 SD 卡后重新进入应用", message, esp_err_to_name(error));
    lv_obj_set_width(label, 420);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xfca5a5), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 12);
}
}  // namespace

void WatchApplications::CreateNovel() {
    novel_path_.clear();
    novel_page_history_.clear();
    novel_page_start_ = novel_next_offset_ = novel_file_size_ = 0;
    RefreshNovelList();
}

void WatchApplications::RefreshNovelList() {
    lv_obj_clean(overlay_);
    novel_list_ = novel_content_label_ = novel_progress_label_ = nullptr;
    novel_previous_button_ = novel_next_button_ = nullptr;
    CreatePageTitle(overlay_, "小说");

    std::vector<WatchStorage::Entry> all_entries;
    const esp_err_t error = WatchStorage::Instance().ListDirectory("/小说", &all_entries);
    if (error != ESP_OK) {
        ShowStorageError(overlay_, error == ESP_ERR_NOT_FOUND ? "未找到 /小说 文件夹" : "SD 卡读取失败", error);
        return;
    }

    novel_entries_.clear();
    for (auto& entry : all_entries) {
        if (!entry.is_directory && HasExtension(entry.name, ".txt")) novel_entries_.push_back(std::move(entry));
    }
    std::sort(novel_entries_.begin(), novel_entries_.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });

    novel_list_ = lv_obj_create(overlay_);
    lv_obj_set_size(novel_list_, 448, 260);
    lv_obj_align(novel_list_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(novel_list_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_width(novel_list_, 0, 0);
    lv_obj_set_style_pad_all(novel_list_, 8, 0);
    lv_obj_set_style_pad_row(novel_list_, 6, 0);
    lv_obj_set_flex_flow(novel_list_, LV_FLEX_FLOW_COLUMN);

    if (novel_entries_.empty()) {
        lv_obj_t* empty = lv_label_create(novel_list_);
        lv_label_set_text(empty, "没有找到 TXT 小说\n请把 UTF-8 文本放入 SD 卡的 /小说 文件夹");
        lv_obj_set_width(empty, 410);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9ca3af), 0);
        return;
    }

    for (size_t index = 0; index < novel_entries_.size(); ++index) {
        lv_obj_t* button = CreateStorageButton(novel_list_, novel_entries_[index].name.c_str(), 416, 46,
                                               lv_color_hex(0x263244));
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index + 1));
        lv_obj_add_event_cb(button, NovelItemCallback, LV_EVENT_CLICKED, this);
    }
}

void WatchApplications::NovelItemCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    const size_t encoded = reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (self != nullptr && encoded > 0) self->OpenNovel(encoded - 1);
}

void WatchApplications::OpenNovel(size_t index) {
    if (index >= novel_entries_.size()) return;
    novel_path_ = "/小说/" + novel_entries_[index].name;
    novel_page_history_.clear();
    uint32_t saved_offset = 0;
    WatchStorage::Instance().LoadReadingOffset(novel_path_, &saved_offset);

    lv_obj_clean(overlay_);
    lv_obj_t* list_button = CreateStorageButton(overlay_, "目录", 72, 38, lv_color_hex(0x374151));
    lv_obj_set_pos(list_button, 12, 8);
    lv_obj_add_event_cb(list_button, NovelListCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* title = CreatePageTitle(overlay_, novel_entries_[index].name.c_str());
    lv_obj_set_width(title, 285);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    novel_progress_label_ = lv_label_create(overlay_);
    lv_obj_set_style_text_color(novel_progress_label_, lv_color_hex(0x9ca3af), 0);
    lv_obj_align(novel_progress_label_, LV_ALIGN_TOP_RIGHT, -16, 18);

    lv_obj_t* content = lv_obj_create(overlay_);
    lv_obj_set_size(content, 452, 222);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xf6f0df), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 10, 0);
    lv_obj_set_style_pad_all(content, 12, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    novel_content_label_ = lv_label_create(content);
    lv_obj_set_width(novel_content_label_, 426);
    lv_label_set_long_mode(novel_content_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(novel_content_label_, lv_color_hex(0x27231c), 0);
    lv_obj_align(novel_content_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    novel_previous_button_ = CreateStorageButton(overlay_, "上一页", 110, 38, lv_color_hex(0x374151));
    novel_next_button_ = CreateStorageButton(overlay_, "下一页", 110, 38, lv_color_hex(0x2563eb));
    lv_obj_align(novel_previous_button_, LV_ALIGN_BOTTOM_LEFT, 14, -6);
    lv_obj_align(novel_next_button_, LV_ALIGN_BOTTOM_RIGHT, -14, -6);
    lv_obj_add_event_cb(novel_previous_button_, NovelPreviousCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(novel_next_button_, NovelNextCallback, LV_EVENT_CLICKED, this);
    ShowNovelPage(saved_offset, false);
}

void WatchApplications::ShowNovelPage(size_t offset, bool remember_current) {
    if (novel_content_label_ == nullptr || novel_path_.empty()) return;
    std::string text;
    size_t raw_next = offset;
    const esp_err_t error = WatchStorage::Instance().ReadChunk(
        novel_path_, offset, kNovelPageBytes, &text, &raw_next, &novel_file_size_);
    if (error != ESP_OK) {
        lv_label_set_text_fmt(novel_content_label_, "读取失败：%s", esp_err_to_name(error));
        return;
    }

    /* 文件块末端若切在 UTF-8 多字节字符中间，则回退到该字符首字节。 */
    size_t valid_bytes = text.size();
    if (valid_bytes == kNovelPageBytes && !text.empty()) {
        size_t lead = valid_bytes - 1;
        while (lead > 0 && (static_cast<unsigned char>(text[lead]) & 0xc0U) == 0x80U) --lead;
        if (lead + Utf8Length(static_cast<unsigned char>(text[lead])) > valid_bytes) valid_bytes = lead;
        text.resize(valid_bytes);
    }
    if (remember_current && offset != novel_page_start_) novel_page_history_.push_back(novel_page_start_);
    novel_page_start_ = offset;
    novel_next_offset_ = offset + valid_bytes;
    lv_label_set_text(novel_content_label_, text.empty() ? "（已到文件末尾）" : text.c_str());
    const unsigned progress = novel_file_size_ == 0 ? 0U :
        static_cast<unsigned>(std::min<size_t>(100, offset * 100 / novel_file_size_));
    lv_label_set_text_fmt(novel_progress_label_, "%u%%", progress);
    if (novel_page_history_.empty()) lv_obj_add_state(novel_previous_button_, LV_STATE_DISABLED);
    else lv_obj_remove_state(novel_previous_button_, LV_STATE_DISABLED);
    if (novel_next_offset_ >= novel_file_size_) lv_obj_add_state(novel_next_button_, LV_STATE_DISABLED);
    else lv_obj_remove_state(novel_next_button_, LV_STATE_DISABLED);
}

void WatchApplications::NovelPreviousCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr || self->novel_page_history_.empty()) return;
    const size_t offset = self->novel_page_history_.back();
    self->novel_page_history_.pop_back();
    self->ShowNovelPage(offset, false);
}

void WatchApplications::NovelNextCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr || self->novel_next_offset_ >= self->novel_file_size_) return;
    self->ShowNovelPage(self->novel_next_offset_, true);
}

void WatchApplications::NovelListCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self != nullptr) self->ReturnToNovelList();
}

void WatchApplications::ReturnToNovelList() {
    if (!novel_path_.empty()) {
        WatchStorage::Instance().SaveReadingOffset(novel_path_, static_cast<uint32_t>(novel_page_start_));
    }
    novel_path_.clear();
    novel_page_history_.clear();
    RefreshNovelList();
}

void WatchApplications::CreateFileManager() {
    file_manager_path_ = "/";
    file_manager_page_ = 0;
    file_details_dialog_ = file_delete_label_ = nullptr;
    file_delete_armed_ = false;

    lv_obj_t* back = CreateStorageButton(overlay_, "返回", 72, 38, lv_color_hex(0x374151));
    lv_obj_set_pos(back, 12, 8);
    lv_obj_add_event_cb(back, FileManagerBackCallback, LV_EVENT_CLICKED, this);
    file_manager_title_ = CreatePageTitle(overlay_, "/");
    lv_obj_set_width(file_manager_title_, 350);
    lv_label_set_long_mode(file_manager_title_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(file_manager_title_, LV_TEXT_ALIGN_CENTER, 0);

    file_manager_list_ = lv_obj_create(overlay_);
    lv_obj_set_size(file_manager_list_, 452, 260);
    lv_obj_align(file_manager_list_, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(file_manager_list_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_width(file_manager_list_, 0, 0);
    lv_obj_set_style_pad_all(file_manager_list_, 8, 0);
    lv_obj_set_style_pad_row(file_manager_list_, 6, 0);
    lv_obj_set_flex_flow(file_manager_list_, LV_FLEX_FLOW_COLUMN);
    RefreshFileManager();
}

void WatchApplications::RefreshFileManager() {
    if (file_manager_list_ == nullptr) return;
    lv_obj_clean(file_manager_list_);
    lv_label_set_text(file_manager_title_, file_manager_path_.c_str());
    const esp_err_t error = WatchStorage::Instance().ListDirectory(file_manager_path_, &file_manager_entries_);
    if (error != ESP_OK) {
        lv_obj_t* label = lv_label_create(file_manager_list_);
        lv_label_set_text_fmt(label, "目录读取失败：%s", esp_err_to_name(error));
        lv_obj_set_style_text_color(label, lv_color_hex(0xfca5a5), 0);
        return;
    }
    std::sort(file_manager_entries_.begin(), file_manager_entries_.end(), [](const auto& left, const auto& right) {
        if (left.is_directory != right.is_directory) return left.is_directory > right.is_directory;
        return left.name < right.name;
    });

    const size_t first = file_manager_page_ * kFilesPerPage;
    if (first >= file_manager_entries_.size() && file_manager_page_ > 0) {
        --file_manager_page_;
        return RefreshFileManager();
    }
    if (file_manager_page_ > 0) {
        lv_obj_t* top = CreateStorageButton(file_manager_list_, "回到顶部", 420, 44, lv_color_hex(0x1d4ed8));
        lv_obj_add_event_cb(top, FileManagerTopCallback, LV_EVENT_CLICKED, this);
    }
    const size_t end = std::min(file_manager_entries_.size(), first + kFilesPerPage);
    for (size_t index = first; index < end; ++index) {
        const auto& entry = file_manager_entries_[index];
        const std::string caption = std::string(entry.is_directory ? LV_SYMBOL_DIRECTORY "  " : LV_SYMBOL_FILE "  ") + entry.name;
        lv_obj_t* button = CreateStorageButton(file_manager_list_, caption.c_str(), 420, 44,
                                               entry.is_directory ? lv_color_hex(0x334155) : lv_color_hex(0x202938));
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index + 1));
        lv_obj_add_event_cb(button, FileManagerItemCallback, LV_EVENT_CLICKED, this);
    }
    if (end < file_manager_entries_.size()) {
        lv_obj_t* next = CreateStorageButton(file_manager_list_, "下一页", 420, 44, lv_color_hex(0x1d4ed8));
        lv_obj_add_event_cb(next, FileManagerNextCallback, LV_EVENT_CLICKED, this);
    }
    if (file_manager_entries_.empty()) {
        lv_obj_t* label = lv_label_create(file_manager_list_);
        lv_label_set_text(label, "此文件夹为空");
        lv_obj_set_style_text_color(label, lv_color_hex(0x9ca3af), 0);
    }
}

void WatchApplications::FileManagerItemCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    const size_t encoded = reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    if (self != nullptr && encoded > 0) self->OpenFileManagerEntry(encoded - 1);
}

void WatchApplications::OpenFileManagerEntry(size_t index) {
    if (index >= file_manager_entries_.size()) return;
    const auto& entry = file_manager_entries_[index];
    if (!entry.is_directory) {
        ShowFileDetails(index);
        return;
    }
    const std::string candidate = file_manager_path_ == "/" ? "/" + entry.name :
                                  file_manager_path_ + "/" + entry.name;
    if (candidate.size() >= 240) return;
    file_manager_path_ = candidate;
    file_manager_page_ = 0;
    RefreshFileManager();
}

void WatchApplications::FileManagerBackCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    if (self->file_manager_path_ == "/") {
        self->Close();
        return;
    }
    const size_t slash = self->file_manager_path_.find_last_of('/');
    self->file_manager_path_ = slash == 0 ? "/" : self->file_manager_path_.substr(0, slash);
    self->file_manager_page_ = 0;
    self->RefreshFileManager();
}

void WatchApplications::FileManagerTopCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    self->file_manager_page_ = 0;
    self->RefreshFileManager();
}

void WatchApplications::FileManagerNextCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr) return;
    ++self->file_manager_page_;
    self->RefreshFileManager();
}

void WatchApplications::ShowFileDetails(size_t index) {
    if (index >= file_manager_entries_.size()) return;
    CloseFileDetails();
    file_details_index_ = index;
    file_delete_armed_ = false;
    const auto& entry = file_manager_entries_[index];

    file_details_dialog_ = lv_obj_create(overlay_);
    lv_obj_set_size(file_details_dialog_, 370, 220);
    lv_obj_center(file_details_dialog_);
    lv_obj_set_style_bg_color(file_details_dialog_, lv_color_hex(0xf9fafb), 0);
    lv_obj_set_style_border_color(file_details_dialog_, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_border_width(file_details_dialog_, 2, 0);
    lv_obj_set_style_radius(file_details_dialog_, 18, 0);
    lv_obj_set_style_pad_all(file_details_dialog_, 18, 0);
    lv_obj_remove_flag(file_details_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* path = lv_label_create(file_details_dialog_);
    const std::string full_path = file_manager_path_ == "/" ? "/" + entry.name :
                                  file_manager_path_ + "/" + entry.name;
    lv_label_set_text(path, full_path.c_str());
    lv_obj_set_width(path, 330);
    lv_label_set_long_mode(path, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(path, lv_color_hex(0x111827), 0);
    lv_obj_align(path, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* size = lv_label_create(file_details_dialog_);
    if (entry.size < 1024) lv_label_set_text_fmt(size, "大小：%llu B", static_cast<unsigned long long>(entry.size));
    else if (entry.size < 1024 * 1024) lv_label_set_text_fmt(size, "大小：%.1f KB", entry.size / 1024.0);
    else lv_label_set_text_fmt(size, "大小：%.1f MB", entry.size / (1024.0 * 1024.0));
    lv_obj_set_style_text_color(size, lv_color_hex(0x475569), 0);
    lv_obj_align(size, LV_ALIGN_LEFT_MID, 0, 12);

    lv_obj_t* close = CreateStorageButton(file_details_dialog_, "关闭", 130, 44, lv_color_hex(0x64748b));
    lv_obj_t* remove = CreateStorageButton(file_details_dialog_, "删除", 130, 44, lv_color_hex(0xdc2626));
    file_delete_label_ = lv_obj_get_child(remove, 0);
    lv_obj_align(close, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(remove, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(close, FileDetailsCloseCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(remove, FileDetailsDeleteCallback, LV_EVENT_CLICKED, this);
    lv_obj_move_foreground(file_details_dialog_);
}

void WatchApplications::CloseFileDetails() {
    if (file_details_dialog_ != nullptr) lv_obj_delete(file_details_dialog_);
    file_details_dialog_ = file_delete_label_ = nullptr;
    file_delete_armed_ = false;
}

void WatchApplications::FileDetailsCloseCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self != nullptr) self->CloseFileDetails();
}

void WatchApplications::FileDetailsDeleteCallback(lv_event_t* event) {
    auto* self = static_cast<WatchApplications*>(lv_event_get_user_data(event));
    if (self == nullptr || self->file_details_index_ >= self->file_manager_entries_.size()) return;
    if (!self->file_delete_armed_) {
        self->file_delete_armed_ = true;
        lv_label_set_text(self->file_delete_label_, "确定删除");
        return;
    }
    const auto& entry = self->file_manager_entries_[self->file_details_index_];
    const std::string path = self->file_manager_path_ == "/" ? "/" + entry.name :
                             self->file_manager_path_ + "/" + entry.name;
    const esp_err_t error = WatchStorage::Instance().RemoveFile(path);
    ESP_LOGI(kTag, "Delete file %s: %s", path.c_str(), esp_err_to_name(error));
    self->CloseFileDetails();
    self->RefreshFileManager();
}
