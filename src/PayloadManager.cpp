#include "PayloadManager.h"

#include <algorithm>
#include <esp_partition.h>

namespace {
constexpr uint32_t MANTIS_FS_OFFSET = 0x290000;
constexpr uint32_t MANTIS_FS_SIZE = 0x570000;
}

PayloadManager::PayloadManager()
    : currentPath("/payloads"), backend(CacheBackend::NONE) {
}

bool PayloadManager::isOwnedLittleFsPartition() const {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr
    );

    return partition != nullptr &&
           partition->address == MANTIS_FS_OFFSET &&
           partition->size == MANTIS_FS_SIZE;
}

bool PayloadManager::begin() {
    backend = CacheBackend::NONE;
    currentPath = "/payloads";
    currentFiles.clear();
    ramFiles.clear();

    if (isOwnedLittleFsPartition()) {
        if (LittleFS.begin(true)) {
            if (!LittleFS.exists("/payloads") && !LittleFS.mkdir("/payloads")) {
                Serial.println("Cannot create /payloads cache directory");
            } else {
                backend = CacheBackend::LITTLEFS;
                Serial.println("PAYLOAD CACHE: LittleFS");
                refresh();
                return true;
            }
        } else {
            Serial.println("LittleFS Mount Failed");
        }
    } else {
        Serial.println("PAYLOAD CACHE: MantisHID LittleFS partition not present");
    }

    backend = CacheBackend::RAM;
    Serial.println("PAYLOAD CACHE: RAM fallback (launcher compatible)");
    refresh();
    return true;
}

bool PayloadManager::isSupportedPayloadFile(const String& name) const {
    String value(name);
    value.toLowerCase();
    return value.endsWith(".txt") || value.endsWith(".duck") || value.endsWith(".ds");
}

void PayloadManager::scanDirectory(const String& path) {
    currentFiles.clear();

    if (backend == CacheBackend::RAM) {
        String prefix = path;
        if (!prefix.endsWith("/")) prefix += "/";

        for (const RamPayload& payload : ramFiles) {
            if (!payload.path.startsWith(prefix)) continue;

            const String relative = payload.path.substring(prefix.length());
            if (relative.length() == 0) continue;

            const int slash = relative.indexOf('/');
            const bool isDirectory = slash >= 0;
            const String name = isDirectory ? relative.substring(0, slash) : relative;
            if (!isDirectory && !isSupportedPayloadFile(name)) continue;

            bool alreadyAdded = false;
            for (const FileEntry& existing : currentFiles) {
                if (existing.name == name && existing.isDir == isDirectory) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (alreadyAdded) continue;
            if (currentFiles.size() >= MAX_FILES) break;

            FileEntry entry;
            entry.name = name;
            entry.isDir = isDirectory;
            entry.size = isDirectory ? 0U : static_cast<uint32_t>(payload.content.length());
            currentFiles.push_back(entry);
        }
    } else if (backend == CacheBackend::LITTLEFS) {
        File root = LittleFS.open(path);
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            return;
        }

        File file = root.openNextFile();
        while (file) {
            if (currentFiles.size() >= MAX_FILES) {
                file.close();
                break;
            }

            String fileName = String(file.name());
            const int lastSlash = fileName.lastIndexOf('/');
            if (lastSlash >= 0) {
                fileName = fileName.substring(lastSlash + 1);
            }

            const bool isDirectory = file.isDirectory();
            if (isDirectory || isSupportedPayloadFile(fileName)) {
                FileEntry entry;
                entry.name = fileName;
                entry.isDir = isDirectory;
                entry.size = isDirectory ? 0U : static_cast<uint32_t>(file.size());
                currentFiles.push_back(entry);
            }

            file.close();
            file = root.openNextFile();
        }
        root.close();
    }

    std::sort(currentFiles.begin(), currentFiles.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDir != b.isDir) {
            return a.isDir && !b.isDir;
        }

        String left(a.name);
        String right(b.name);
        left.toLowerCase();
        right.toLowerCase();
        return left.compareTo(right) < 0;
    });
}

void PayloadManager::refresh() {
    scanDirectory(currentPath);
}

void PayloadManager::navigateUp() {
    if (currentPath == "/payloads") {
        return;
    }

    const int lastSlash = currentPath.lastIndexOf('/');
    if (lastSlash <= 9) {
        currentPath = "/payloads";
    } else {
        currentPath = currentPath.substring(0, lastSlash);
    }
    refresh();
}

bool PayloadManager::ramDirectoryExists(const String& path) const {
    String prefix = path;
    if (!prefix.endsWith("/")) prefix += "/";

    for (const RamPayload& payload : ramFiles) {
        if (payload.path.startsWith(prefix)) return true;
    }
    return false;
}

bool PayloadManager::navigateDown(const String& name) {
    String newPath = currentPath;
    if (newPath != "/") {
        newPath += "/";
    }
    newPath += name;

    if (backend == CacheBackend::RAM) {
        if (ramDirectoryExists(newPath)) {
            currentPath = newPath;
            refresh();
            return true;
        }
        return false;
    }

    if (backend == CacheBackend::LITTLEFS) {
        File file = LittleFS.open(newPath);
        if (file && file.isDirectory()) {
            currentPath = newPath;
            file.close();
            refresh();
            return true;
        }

        if (file) {
            file.close();
        }
    }
    return false;
}

std::vector<FileEntry> PayloadManager::getFileList() {
    return currentFiles;
}

String PayloadManager::getCurrentPath() {
    return currentPath;
}

String PayloadManager::getRelativePath() {
    if (currentPath == "/payloads") {
        return "/";
    }
    if (currentPath.startsWith("/payloads")) {
        return currentPath.substring(9);
    }
    return currentPath;
}

String PayloadManager::buildFilePath(const String& filename) const {
    String fullPath = currentPath;
    if (fullPath != "/") {
        fullPath += "/";
    }
    fullPath += filename;
    return fullPath;
}

String PayloadManager::loadFile(const String& filename) {
    const String fullPath = buildFilePath(filename);

    if (backend == CacheBackend::RAM) {
        for (const RamPayload& payload : ramFiles) {
            if (payload.path == fullPath) {
                if (payload.content.length() > 20000) return "";
                return payload.content;
            }
        }
        return "";
    }

    if (backend == CacheBackend::LITTLEFS) {
        File file = LittleFS.open(fullPath, FILE_READ);
        if (!file) {
            return "";
        }

        if (file.size() > 20000) {
            file.close();
            return "";
        }

        String content;
        content.reserve(file.size() + 1);
        while (file.available()) {
            content += static_cast<char>(file.read());
        }
        file.close();
        return content;
    }

    return "";
}

void PayloadManager::returnToRoot() {
    currentPath = "/payloads";
    refresh();
}

uint32_t PayloadManager::countRecursive(const String& path, uint8_t depth) const {
    if (backend != CacheBackend::LITTLEFS || depth > 6) {
        return 0;
    }

    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }

    uint32_t count = 0;
    File file = root.openNextFile();
    while (file) {
        String childPath = String(file.path());
        String fileName = String(file.name());
        const int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash >= 0) {
            fileName = fileName.substring(lastSlash + 1);
        }
        const bool isDirectory = file.isDirectory();
        file.close();

        if (isDirectory) {
            count += countRecursive(childPath, depth + 1);
        } else if (isSupportedPayloadFile(fileName)) {
            count++;
        }

        file = root.openNextFile();
    }
    root.close();
    return count;
}

uint32_t PayloadManager::countPayloadFiles() const {
    if (backend == CacheBackend::RAM) {
        uint32_t count = 0;
        for (const RamPayload& payload : ramFiles) {
            const int slash = payload.path.lastIndexOf('/');
            const String name = slash >= 0 ? payload.path.substring(slash + 1) : payload.path;
            if (isSupportedPayloadFile(name)) count++;
        }
        return count;
    }
    return countRecursive("/payloads", 0);
}

bool PayloadManager::ensureCacheDirectory(const String& path) {
    if (backend == CacheBackend::RAM) {
        (void)path;
        return true;
    }
    if (backend != CacheBackend::LITTLEFS) return false;
    return LittleFS.exists(path) || LittleFS.mkdir(path);
}

bool PayloadManager::clearCacheDirectory(const String& path) {
    if (backend == CacheBackend::RAM) {
        String prefix = path;
        if (!prefix.endsWith("/")) prefix += "/";
        ramFiles.erase(
            std::remove_if(ramFiles.begin(), ramFiles.end(), [&](const RamPayload& payload) {
                return payload.path == path || payload.path.startsWith(prefix);
            }),
            ramFiles.end()
        );
        refresh();
        return true;
    }

    if (backend != CacheBackend::LITTLEFS) return false;

    File root = LittleFS.open(path);
    if (!root) {
        return LittleFS.mkdir(path);
    }
    if (!root.isDirectory()) {
        root.close();
        if (!LittleFS.remove(path)) {
            return false;
        }
        return LittleFS.mkdir(path);
    }

    std::vector<String> children;
    File child = root.openNextFile();
    while (child) {
        children.push_back(String(child.path()));
        child.close();
        child = root.openNextFile();
        delay(1);
    }
    root.close();

    for (const String& childPath : children) {
        File entry = LittleFS.open(childPath);
        if (!entry) continue;
        const bool isDir = entry.isDirectory();
        entry.close();

        if (isDir) {
            if (!clearCacheDirectory(childPath)) return false;
            if (!LittleFS.rmdir(childPath)) return false;
        } else if (!LittleFS.remove(childPath)) {
            return false;
        }
        delay(1);
    }

    refresh();
    return true;
}

bool PayloadManager::writeCacheFile(const String& path, const String& content) {
    if (backend == CacheBackend::RAM) {
        for (RamPayload& payload : ramFiles) {
            if (payload.path == path) {
                payload.content = content;
                return true;
            }
        }

        if (ramFiles.size() >= MAX_FILES) return false;
        RamPayload payload;
        payload.path = path;
        payload.content = content;
        ramFiles.push_back(payload);
        return true;
    }

    if (backend != CacheBackend::LITTLEFS) return false;

    File destination = LittleFS.open(path, FILE_WRITE);
    if (!destination) return false;

    const size_t written = destination.write(
        reinterpret_cast<const uint8_t*>(content.c_str()),
        content.length()
    );
    destination.close();

    if (written != content.length()) {
        LittleFS.remove(path);
        return false;
    }
    return true;
}

bool PayloadManager::usingRamCache() const {
    return backend == CacheBackend::RAM;
}

bool PayloadManager::usingLittleFsCache() const {
    return backend == CacheBackend::LITTLEFS;
}

const char* PayloadManager::cacheBackendName() const {
    if (backend == CacheBackend::LITTLEFS) return "LittleFS";
    if (backend == CacheBackend::RAM) return "RAM";
    return "NONE";
}
