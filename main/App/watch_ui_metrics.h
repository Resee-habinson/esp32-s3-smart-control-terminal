#ifndef WATCH_UI_METRICS_H_
#define WATCH_UI_METRICS_H_

/**
 * 手表 UI 在 480×320 物理 LCD 中央使用的原版竖屏有效视口。
 *
 * 视口直接作为所有手表页面的父对象，不使用运行时缩放图层。这样 LVGL 只会
 * 生成中央 240×280 区域的无效矩形，LCD 两侧和上下黑边在初始化后不再刷新。
 */
namespace WatchUiMetrics {
constexpr int kPhysicalWidth = 480;
constexpr int kPhysicalHeight = 320;
constexpr int kWidth = 240;
constexpr int kHeight = 280;
constexpr int kOffsetX = (kPhysicalWidth - kWidth) / 2;
constexpr int kOffsetY = (kPhysicalHeight - kHeight) / 2;
constexpr int kStatusBarHeight = 24;
constexpr int kPageTop = 26;
constexpr int kPagePadding = 6;
constexpr int kContentWidth = kWidth - 2 * kPagePadding;
}  // namespace WatchUiMetrics

#endif  // WATCH_UI_METRICS_H_
