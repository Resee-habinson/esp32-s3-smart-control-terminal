#ifndef LVGL_DISPLAY_H
#define LVGL_DISPLAY_H

#include "display.h"
#include "lvgl_image.h"

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class DynamicGlyphCache;
class LvglFont;

class LvglDisplay : public Display {
public:
    LvglDisplay();
    virtual ~LvglDisplay();

    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000);
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image);
    virtual void UpdateStatusBar(bool update_all = false);
    virtual void SetPowerSaveMode(bool on);
    virtual bool SnapshotToJpeg(std::string& jpeg_data, int quality = 80);
    virtual bool AddTextGlyphs(const std::vector<TextGlyph>& glyphs, uint8_t bpp) override;
    virtual void ClearTextGlyphs() override;
    bool SetTextFont(std::shared_ptr<LvglFont> text_font);

protected:
    esp_pm_lock_handle_t pm_lock_ = nullptr;
    lv_display_t* display_ = nullptr;

    lv_obj_t* network_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* notification_label_ = nullptr;
    lv_obj_t* mute_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* low_battery_popup_ = nullptr;
    lv_obj_t* low_battery_label_ = nullptr;

    const char* battery_icon_ = nullptr;
    const char* network_icon_ = nullptr;
    bool muted_ = false;

    std::chrono::system_clock::time_point last_status_update_time_;
    esp_timer_handle_t notification_timer_ = nullptr;
    std::unique_ptr<DynamicGlyphCache> dynamic_glyph_cache_;
    /*
     * LVGL 样式只保存 lv_font_t 裸指针。主题运行时替换字体后，已经创建的自定义页面
     * 仍可能引用旧字体，因此必须把旧字体所有者保留到 Display 析构，防止绘制时悬空。
     */
    std::vector<std::shared_ptr<LvglFont>> retired_text_fonts_;

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;
};

#endif
