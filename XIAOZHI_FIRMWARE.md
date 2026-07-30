# 小智 AI 官方完整预编译固件（Touch-LCD-3.5 摄像头支持）

本目录使用 **Waveshare ESP32-S3-Touch-LCD-3.5** 对应的小智 AI 官方固件。
这款固件本身已经启用 OV2640 和 OV5640 摄像头，不需要改用名称中带
`cam-3.5` 的固件。

## 重要的板型命名区别

- 正确：`waveshare-esp32-s3-touch-lcd-3.5`
  - 对应本机一体式 3.5 英寸触摸屏开发板。
  - 官方配置启用了 OV2640、OV5640，并实现摄像头初始化。
- 不兼容：`waveshare-esp32-s3-cam-3.5`
  - 对应另一系列 `ESP32-S3-CAM-XXXX` 摄像头主板外接 3.5 英寸显示屏。
  - 在本机实测会导致 CH32V003 IO 扩展器初始化失败，随后 LVGL
    `LoadProhibited` 崩溃并循环重启。

## 已准备的正确固件

- 上游项目：<https://github.com/78/xiaozhi-esp32>
- Release：`v2.4.0`
- 板型：`waveshare-esp32-s3-touch-lcd-3.5`
- 合并镜像：
  `firmware/xiaozhi-v2.4.0-waveshare-esp32-s3-touch-lcd-3.5/merged-binary.bin`
- 镜像大小：`10,052,777` bytes
- Release ZIP SHA-256：
  `B11295340E90B7539A98E53BA0315A4E1842132C0916593868EB97AC2D460415`
- 合并镜像 SHA-256：
  `C8BCA538C6855763CBBEDCC55CE7F5CEC3AE2888C3DCD8B3231E388DDFEFBCAF`

`esptool image-info` 已确认该文件是校验有效的 ESP32-S3 镜像，目标 Flash
为 16 MB、DIO、80 MHz，Bootloader 使用 ESP-IDF 6.0.1。

## Windows 一键烧录

1. 确认摄像头排线已经可靠插入并锁紧。
2. 使用可传输数据的 USB-C 线连接开发板。
3. 在设备管理器中确认新出现的 `COM` 端口。
4. 在本目录打开 PowerShell。默认烧录页面流畅版：

   ```powershell
   .\tools\flash_xiaozhi.ps1 -Port COM10
   ```

   如需恢复未经修改的官方版：

   ```powershell
   .\tools\flash_xiaozhi.ps1 -Port COM10 -Edition Official
   ```

脚本会先核对所选固件的 SHA-256，擦除整片 Flash，再将完整合并镜像写入
`0x0` 并由 `esptool` 校验。

## 页面流畅版

页面流畅版仍基于官方 `v2.4.0` 和正确的摄像头板型，只调整 LCD/LVGL
刷新链路：

- LCD SPI 像素时钟由 40 MHz 提高为 60 MHz。
- LVGL 从单个 20 行 DMA 缓冲区改为两个 20 行 DMA 缓冲区。
- 增加约 19.2 KiB 内部 DMA 内存占用。
- 保留 OV2640、OV5640 自动检测；不是修改摄像头帧率。

构建产物：

- 合并镜像：
  `firmware/xiaozhi-v2.4.0-waveshare-esp32-s3-touch-lcd-3.5-ui-performance/merged-binary.bin`
- 镜像大小：`10,052,873` bytes
- 合并镜像 SHA-256：
  `8C66E95FD2D41A97139C2B6C74E9969E18C596010AD83235A74EE7FFFC4AC454`
- Release ZIP SHA-256：
  `045A98E23980DCEED103D713E13FBFCB38344B6EEF07E4E9128140EA5E6DBD4E`
- 完整构建通过，应用分区尚余约 30%。

理论上的全屏 RGB565 总线刷新上限从约 16.3 FPS 提高到约 24.4 FPS；
实际滑动帧率还受 LVGL 绘制、CPU 负载和刷新区域影响。60 MHz 的屏幕信号
稳定性仍需要实机观察；若出现花屏、闪烁或撕裂，可恢复官方版。

## ESP-IDF 开发环境

- ESP-IDF：`D:\Espressif\frameworks\esp-idf-v6.0.2`
- Python：`D:\Espressif\python_env\idf6.0_py3.14_env`
- 工具目录：`D:\Espressif\tools`
- VS Code 工作区设置已切换到上述路径。
- 原 `ESP-IDF 5.5.4` 框架已删除；当前配置不再引用旧版环境。

## 首次启动

烧录后按照屏幕和语音提示完成 Wi-Fi 配置。设备连接官方服务后，如果显示或
播报激活码，请登录 <https://xiaozhi.me/>，在控制台添加设备并填写激活码。

## 验收项目

- 启动日志不再出现连续 Guru Meditation 重启。
- LCD 背光与画面正常。
- 触摸正常。
- 麦克风和扬声器正常。
- OV2640/OV5640 摄像头能够取景。
- Wi-Fi 配网、设备激活和语音对话正常。

## 本次实机结果

- 已在 `COM10` 擦除整片 Flash，并写入正确的
  `waveshare-esp32-s3-touch-lcd-3.5` v2.4.0 合并镜像。
- `esptool` 写入后哈希验证通过。
- 串口日志持续运行到至少 51 秒，未再出现 `Guru Meditation`、panic
  或软件复位；此前错误 `cam-3.5` 固件造成的循环重启已消失。
- 仍需人工观察并操作屏幕，验收触摸、音频、Wi-Fi、激活和摄像头实时取景。

参考：

- <https://docs.waveshare.net/ESP32-S3-Touch-LCD-3.5/XiaoZhi_AI/>
- <https://github.com/78/xiaozhi-esp32/releases/tag/v2.4.0>
- <https://github.com/78/xiaozhi-esp32/tree/v2.4.0/main/boards/waveshare/esp32-s3-touch-lcd-3.5>
