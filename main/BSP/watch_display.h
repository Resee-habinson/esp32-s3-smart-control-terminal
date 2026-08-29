#ifndef WATCH_DISPLAY_H_
#define WATCH_DISPLAY_H_

#include "App/watch_app_shell.h"
#include "display/lcd_display.h"

/** 在现有 SPI LCD 驱动之上组合手表 App，不修改小智显示实现。 */
class WatchDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    /** 函数：先创建原小智 UI，再创建独立手表 Screen；参数：无；返回值：无 */
    void SetupUI() override;

    /** 函数：切换手表/小智应用；参数：无；返回值：无 */
    void ToggleApplication() { shell_.Toggle(); }

private:
    WatchAppShell shell_;
};

#endif  // WATCH_DISPLAY_H_
