#ifndef PAYLOAD_MANAGER_H
#define PAYLOAD_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>

struct FileEntry {
    String name;
    bool isDir;
    uint32_t size;
};

class PayloadManager {
private:
    struct RamPayload {
        String path;
        String content;
    };

    enum class CacheBackend : uint8_t {
        NONE,
        LITTLEFS,
        RAM
    };

    String currentPath;
    std::vector<FileEntry> currentFiles;
    std::vector<RamPayload> ramFiles;
    CacheBackend backend;
    static const size_t MAX_FILES = 128;

    void scanDirectory(const String& path);
    bool isSupportedPayloadFile(const String& name) const;
    uint32_t countRecursive(const String& path, uint8_t depth) const;
    bool isOwnedLittleFsPartition() const;
    bool ramDirectoryExists(const String& path) const;

public:
    PayloadManager();
    bool begin();
    void navigateUp();
    bool navigateDown(const String& name);
    std::vector<FileEntry> getFileList();
    String getCurrentPath();
    String getRelativePath();
    String buildFilePath(const String& filename) const;
    String loadFile(const String& filename);
    void refresh();
    void returnToRoot();
    uint32_t countPayloadFiles() const;

    bool ensureCacheDirectory(const String& path);
    bool clearCacheDirectory(const String& path);
    bool writeCacheFile(const String& path, const String& content);
    bool usingRamCache() const;
    bool usingLittleFsCache() const;
    const char* cacheBackendName() const;
};

#endif
