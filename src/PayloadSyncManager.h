#ifndef MANTIS_PAYLOAD_SYNC_MANAGER_H
#define MANTIS_PAYLOAD_SYNC_MANAGER_H

#include <Arduino.h>
#include "SDWholeCardStorage.h"
#include "WholeSdFat32Reader.h"
#include "PayloadManager.h"

struct PayloadSyncStats {
    bool ok;
    bool sourceFound;
    uint32_t filesCopied;
    uint32_t directoriesCreated;
    uint32_t skippedFiles;
    uint32_t bytesCopied;
    String error;
};

class PayloadSyncManager {
public:
    explicit PayloadSyncManager(PayloadManager& cacheManager);
    PayloadSyncStats syncFromWholeSd(SDWholeCardStorage& storage);

private:
    static constexpr uint32_t MAX_FILES = 64;
    static constexpr uint32_t MAX_FILE_BYTES = 20000;
    static constexpr uint32_t MAX_TOTAL_BYTES = 512 * 1024;
    static constexpr uint8_t MAX_DEPTH = 4;

    PayloadSyncStats stats;
    WholeSdFat32Reader reader;
    PayloadManager& cache;

    bool clearCacheDirectory(const String& path);
    bool copyDirectory(const String& sourcePath, const String& cachePath, uint8_t depth);
    bool copyFile(const String& sourcePath, const String& cachePath, uint32_t size);
    bool allowedPayloadName(const String& name) const;
    void fail(const String& message);
};

#endif
