#include "watch_display.h"
#include "watch_storage.h"

#include <esp_log.h>

namespace {
constexpr char kTag[] = "watch_display";
}

void WatchDisplay::SetupUI() {
    /* 基类完整保留小智 UI 的创建顺序和对象结构。 */
    SpiLcdDisplay::SetupUI();
    /*
     * 在 Wi-Fi、TLS 和语音模型占用内部 SRAM 前挂载 SDMMC，为驱动 DMA 描述符保留空间。
     * 未插卡属于可恢复状态，仅记录结果；用户插卡后仍可从设置页执行强制重挂载。
     */
    const esp_err_t storage_error = WatchStorage::Instance().EnsureMounted();
    if (storage_error != ESP_OK) {
        ESP_LOGW(kTag, "Early SD mount skipped: %s", esp_err_to_name(storage_error));
    }
    lv_obj_t* xiaozhi_screen = lv_screen_active();
    if (!shell_.Initialize(xiaozhi_screen)) {
        ESP_LOGE(kTag, "Watch application shell initialization failed");
    }
}
