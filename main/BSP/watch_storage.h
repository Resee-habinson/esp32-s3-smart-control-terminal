#ifndef WATCH_STORAGE_H_
#define WATCH_STORAGE_H_

#include <esp_err.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Waveshare ESP32-S3-Touch-LCD-3.5 的共享 SD 卡服务。
 *
 * 本类隐藏 SDMMC、FATFS 与 NVS 的实现细节。App 层只能提交以“/”开头的
 * SD 卡相对路径，禁止“..”路径穿越。首次访问时按官方例程使用 1-bit
 * SDMMC 懒挂载，后续图片、小说、音乐、视频和文件管理器共享同一挂载实例。
 */
class WatchStorage {
public:
    struct Entry {
        std::string name;
        bool is_directory = false;
        uint64_t size = 0;
    };

    /** 函数：取得进程内唯一实例；参数：无；返回值：存储服务引用 */
    static WatchStorage& Instance();

    /** 函数：确保 SD 卡已挂载；参数：无；返回值：ESP_OK 或具体挂载错误 */
    esp_err_t EnsureMounted();

    /**
     * 函    数：把应用使用的 SD 相对路径转换为 FATFS VFS 路径
     * 参    数：path 必须以“/”开头且不能包含路径穿越；resolved 接收完整路径
     * 返 回 值：ESP_OK 表示路径可用；其他值表示未挂载、参数错误或路径非法
     * 注意事项：返回路径仅用于 fopen/stat 等媒体接口，不转移存储服务所有权
     */
    esp_err_t GetVfsPath(const std::string& path, std::string* resolved);

    /** 函数：列出目录；参数：SD 相对路径、输出数组；返回值：ESP_OK 或错误码 */
    esp_err_t ListDirectory(const std::string& path, std::vector<Entry>* entries);

    /**
     * 函数：读取一段文件内容。
     * 参数：path 文件路径，offset 起始偏移，capacity 最大字节数，data/next_offset/file_size 输出。
     * 返回值：ESP_OK 表示读取完成；错误时输出参数会被清空。
     */
    esp_err_t ReadChunk(const std::string& path, size_t offset, size_t capacity,
                        std::string* data, size_t* next_offset, size_t* file_size);

    /** 函数：删除普通文件；参数：SD 相对路径；返回值：ESP_OK 或错误码 */
    esp_err_t RemoveFile(const std::string& path);

    /** 函数：保存小说阅读偏移；参数：文件路径、页首偏移；返回值：ESP_OK 或 NVS 错误码 */
    esp_err_t SaveReadingOffset(const std::string& path, uint32_t offset);

    /** 函数：读取小说阅读偏移；参数：文件路径、输出偏移；返回值：ESP_OK 或 NVS 错误码 */
    esp_err_t LoadReadingOffset(const std::string& path, uint32_t* offset);

private:
    WatchStorage() = default;
    WatchStorage(const WatchStorage&) = delete;
    WatchStorage& operator=(const WatchStorage&) = delete;

    /** 函数：把受限相对路径转换为 VFS 路径；参数：输入、输出；返回值：路径是否合法 */
    bool ResolvePath(const std::string& path, std::string* resolved) const;

    bool mounted_ = false;
    esp_err_t last_mount_error_ = ESP_ERR_INVALID_STATE;
};

#endif  // WATCH_STORAGE_H_
