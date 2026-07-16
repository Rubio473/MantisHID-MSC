#include "PayloadSyncManager.h"

#include <vector>

PayloadSyncManager::PayloadSyncManager(PayloadManager& cacheManager)
    : cache(cacheManager) {
    stats = {};
}

void PayloadSyncManager::fail(const String& message) {
    stats.ok = false;
    stats.error = message;
    Serial.println("PAYLOAD SYNC ERROR: " + message);
}

bool PayloadSyncManager::allowedPayloadName(const String& name) const {
    String value(name);
    value.toLowerCase();
    return value.endsWith(".txt") || value.endsWith(".duck") || value.endsWith(".ds");
}

bool PayloadSyncManager::clearCacheDirectory(const String& path) {
    return cache.clearCacheDirectory(path);
}

bool PayloadSyncManager::copyFile(const String& sourcePath, const String& cachePath, uint32_t size) {
    if (stats.filesCopied >= MAX_FILES || size > MAX_FILE_BYTES ||
        stats.bytesCopied + size > MAX_TOTAL_BYTES) {
        stats.skippedFiles++;
        return true;
    }

    String content;
    if (!reader.readFile(sourcePath, content, MAX_FILE_BYTES) || content.length() != size) {
        return false;
    }

    if (!cache.writeCacheFile(cachePath, content)) {
        return false;
    }

    stats.filesCopied++;
    stats.bytesCopied += static_cast<uint32_t>(content.length());
    delay(1);
    return true;
}

bool PayloadSyncManager::copyDirectory(const String& sourcePath, const String& cachePath, uint8_t depth) {
    if (depth > MAX_DEPTH) {
        return true;
    }

    std::vector<WholeSdFileEntry> entries;
    if (!reader.listDirectory(sourcePath, entries, 100)) {
        return false;
    }

    for (const WholeSdFileEntry& entry : entries) {
        const String childSource = sourcePath + "/" + entry.name;
        const String childCache = cachePath + "/" + entry.name;

        if (entry.isDir) {
            if (depth >= MAX_DEPTH) {
                continue;
            }
            if (!cache.ensureCacheDirectory(childCache)) {
                return false;
            }
            stats.directoriesCreated++;
            if (!copyDirectory(childSource, childCache, depth + 1)) {
                return false;
            }
        } else if (allowedPayloadName(entry.name)) {
            if (!copyFile(childSource, childCache, entry.size)) {
                return false;
            }
        }
        delay(1);
    }

    return true;
}

PayloadSyncStats PayloadSyncManager::syncFromWholeSd(SDWholeCardStorage& storage) {
    stats = {};
    stats.ok = true;
    stats.error = "OK";

    Serial.println("PAYLOAD SYNC STAGE 1: preflight");
    if (!storage.isReady()) {
        fail("microSD backend is not ready");
        return stats;
    }

    Serial.println("PAYLOAD SYNC STAGE 2: cache ready");
    Serial.printf("PAYLOAD CACHE BACKEND: %s\n", cache.cacheBackendName());
    if (!cache.ensureCacheDirectory("/payloads")) {
        fail("cannot create payload cache root");
        return stats;
    }

    Serial.println("PAYLOAD SYNC STAGE 3: raw FAT32 reader");
    if (!reader.begin(storage)) {
        fail(String("raw FAT32 init failed: ") + reader.lastError());
        return stats;
    }

    Serial.println("PAYLOAD SYNC STAGE 4: clear cache");
    if (!clearCacheDirectory("/payloads")) {
        fail("cannot clear payload cache");
        return stats;
    }

    Serial.println("PAYLOAD SYNC STAGE 5: source directory");
    if (!reader.directoryExists("/MantisSD/Payloads")) {
        stats.sourceFound = false;
        stats.error = "/MantisSD/Payloads missing; cache cleared";
        Serial.println("PAYLOAD SYNC: /MantisSD/Payloads missing, cache is now empty");
        return stats;
    }

    stats.sourceFound = true;
    Serial.println("PAYLOAD SYNC STAGE 6: raw copy");
    if (!copyDirectory("/MantisSD/Payloads", "/payloads", 0)) {
        fail("raw copy from /MantisSD/Payloads failed");
        return stats;
    }

    Serial.println("PAYLOAD SYNC STAGE 7: complete");
    Serial.printf(
        "PAYLOAD SYNC OK: backend=%s files=%lu dirs=%lu skipped=%lu bytes=%lu\n",
        cache.cacheBackendName(),
        static_cast<unsigned long>(stats.filesCopied),
        static_cast<unsigned long>(stats.directoriesCreated),
        static_cast<unsigned long>(stats.skippedFiles),
        static_cast<unsigned long>(stats.bytesCopied)
    );
    return stats;
}
