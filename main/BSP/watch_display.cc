#include "watch_display.h"

#include <esp_log.h>

namespace {
constexpr char kTag[] = "watch_display";
}

void WatchDisplay::SetupUI() {
    /* 基类完整保留小智 UI 的创建顺序和对象结构。 */
    SpiLcdDisplay::SetupUI();
    lv_obj_t* xiaozhi_screen = lv_screen_active();
    if (!shell_.Initialize(xiaozhi_screen)) {
        ESP_LOGE(kTag, "Watch application shell initialization failed");
    }
}
