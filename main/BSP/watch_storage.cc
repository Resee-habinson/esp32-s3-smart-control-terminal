#include "watch_storage.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <nvs.h>
#include <sdmmc_cmd.h>

namespace {
constexpr char kTag[] = "watch_storage";
constexpr char kMountPoint[] = "/sdcard";
constexpr gpio_num_t kSdClock = GPIO_NUM_11;
constexpr gpio_num_t kSdCommand = GPIO_NUM_10;
constexpr gpio_num_t kSdData0 = GPIO_NUM_9;
constexpr size_t kMaximumEntries = 256;

/** 函数：计算稳定的 FNV-1a 路径哈希；参数：路径；返回值：32 位哈希 */
uint32_t HashPath(const std::string& path) {
    uint32_t hash = 2166136261U;
    for (const unsigned char byte : path) {
        hash ^= byte;
        hash *= 16777619U;
    }
    return hash;
}

/** 函数：生成符合 NVS 15 字符限制的键；参数：路径、输出缓冲；返回值：无 */
void MakeProgressKey(const std::string& path, char (&key)[12]) {
    std::snprintf(key, sizeof(key), "n%08lx", static_cast<unsigned long>(HashPath(path)));
}
}  // namespace

WatchStorage& WatchStorage::Instance() {
    static WatchStorage instance;
    return instance;
}

esp_err_t WatchStorage::EnsureMounted() {
    if (mounted_) return ESP_OK;

    /*
     * 严格复用 Waveshare 官方 ESP-IDF 例程的 1-bit SDMMC 接线。这里禁止
     * format_if_mount_failed，避免因卡片损坏或格式不支持而静默清空用户数据。
     */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = kSdClock;
    slot.cmd = kSdCommand;
    slot.d0 = kSdData0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t config = {};
    config.format_if_mount_failed = false;
    config.max_files = 8;
    config.allocation_unit_size = 16 * 1024;

    sdmmc_card_t* card = nullptr;
    last_mount_error_ = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &config, &card);
    if (last_mount_error_ != ESP_OK) {
        ESP_LOGW(kTag, "SD card mount failed: %s", esp_err_to_name(last_mount_error_));
        return last_mount_error_;
    }

    mounted_ = true;
    ESP_LOGI(kTag, "SD card mounted: capacity=%llu MiB",
             static_cast<unsigned long long>(card->csd.capacity * card->csd.sector_size / 1024ULL / 1024ULL));
    return ESP_OK;
}

esp_err_t WatchStorage::GetVfsPath(const std::string& path, std::string* resolved) {
    if (resolved == nullptr) return ESP_ERR_INVALID_ARG;
    const esp_err_t error = EnsureMounted();
    if (error != ESP_OK) return error;
    return ResolvePath(path, resolved) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

bool WatchStorage::ResolvePath(const std::string& path, std::string* resolved) const {
    if (resolved == nullptr || path.empty() || path.front() != '/' ||
        path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
        return false;
    }
    *resolved = std::string(kMountPoint) + path;
    return true;
}

esp_err_t WatchStorage::ListDirectory(const std::string& path, std::vector<Entry>* entries) {
    if (entries == nullptr) return ESP_ERR_INVALID_ARG;
    entries->clear();
    esp_err_t error = EnsureMounted();
    if (error != ESP_OK) return error;

    std::string resolved;
    if (!ResolvePath(path, &resolved)) return ESP_ERR_INVALID_ARG;
    DIR* directory = opendir(resolved.c_str());
    if (directory == nullptr) return ESP_ERR_NOT_FOUND;

    while (entries->size() < kMaximumEntries) {
        dirent* item = readdir(directory);
        if (item == nullptr) break;
        if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0) continue;

        Entry entry;
        entry.name = item->d_name;
        const std::string full_path = resolved + "/" + entry.name;
        struct stat information = {};
        if (stat(full_path.c_str(), &information) == 0) {
            entry.is_directory = S_ISDIR(information.st_mode);
            entry.size = static_cast<uint64_t>(information.st_size);
        } else {
            entry.is_directory = item->d_type == DT_DIR;
        }
        entries->push_back(std::move(entry));
    }
    closedir(directory);
    return ESP_OK;
}

esp_err_t WatchStorage::ReadChunk(const std::string& path, size_t offset, size_t capacity,
                                  std::string* data, size_t* next_offset, size_t* file_size) {
    if (data == nullptr || next_offset == nullptr || file_size == nullptr || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    data->clear();
    *next_offset = offset;
    *file_size = 0;
    esp_err_t error = EnsureMounted();
    if (error != ESP_OK) return error;

    std::string resolved;
    if (!ResolvePath(path, &resolved)) return ESP_ERR_INVALID_ARG;
    FILE* file = std::fopen(resolved.c_str(), "rb");
    if (file == nullptr) return ESP_ERR_NOT_FOUND;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return ESP_FAIL;
    }
    const long length = std::ftell(file);
    if (length < 0 || std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
        std::fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    *file_size = static_cast<size_t>(length);
    data->resize(capacity);
    const size_t bytes_read = std::fread(data->data(), 1, capacity, file);
    if (std::ferror(file) != 0) {
        data->clear();
        std::fclose(file);
        return ESP_FAIL;
    }
    data->resize(bytes_read);
    *next_offset = offset + bytes_read;
    std::fclose(file);
    return ESP_OK;
}

esp_err_t WatchStorage::RemoveFile(const std::string& path) {
    esp_err_t error = EnsureMounted();
    if (error != ESP_OK) return error;
    std::string resolved;
    if (!ResolvePath(path, &resolved) || path == "/") return ESP_ERR_INVALID_ARG;
    struct stat information = {};
    if (stat(resolved.c_str(), &information) != 0) return ESP_ERR_NOT_FOUND;
    if (!S_ISREG(information.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    return unlink(resolved.c_str()) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t WatchStorage::SaveReadingOffset(const std::string& path, uint32_t offset) {
    char key[12] = {};
    MakeProgressKey(path, key);
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("novel_reader", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u32(handle, key, offset);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t WatchStorage::LoadReadingOffset(const std::string& path, uint32_t* offset) {
    if (offset == nullptr) return ESP_ERR_INVALID_ARG;
    *offset = 0;
    char key[12] = {};
    MakeProgressKey(path, key);
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("novel_reader", NVS_READONLY, &handle);
    if (error != ESP_OK) return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
    error = nvs_get_u32(handle, key, offset);
    nvs_close(handle);
    return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
}
