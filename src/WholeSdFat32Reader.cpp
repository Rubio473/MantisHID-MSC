#include "WholeSdFat32Reader.h"

#include <cstring>

WholeSdFat32Reader::WholeSdFat32Reader()
    : storage(nullptr),
      ready(false),
      errorText("not initialized"),
      partitionStart(0),
      partitionSectors(0),
      sectorsPerCluster(0),
      reservedSectors(0),
      fatCount(0),
      fatSizeSectors(0),
      totalVolumeSectors(0),
      fatStartSector(0),
      firstDataSector(0),
      rootCluster(0),
      clusterCount(0) {
    memset(sectorBuffer, 0, sizeof(sectorBuffer));
}

void WholeSdFat32Reader::fail(const char* message) {
    ready = false;
    errorText = message;
    Serial.printf("RAW FAT32 ERROR: %s\n", message);
}

uint16_t WholeSdFat32Reader::readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t WholeSdFat32Reader::readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool WholeSdFat32Reader::begin(SDWholeCardStorage& newStorage) {
    ready = false;
    errorText = "initializing";
    storage = &newStorage;
    partitionStart = 0;
    partitionSectors = 0;

    if (!storage->isReady()) {
        fail("SD backend is not ready");
        return false;
    }

    partitionStart = storage->partitionStartLba();
    partitionSectors = storage->partitionSectorCount();
    if (partitionStart == 0 || partitionSectors == 0) {
        fail("MANTIS partition is missing");
        return false;
    }

    if (!readSector(0, sectorBuffer)) {
        fail("FAT32 boot sector read failed");
        return false;
    }
    if (sectorBuffer[510] != 0x55 || sectorBuffer[511] != 0xAA) {
        fail("FAT32 boot signature is invalid");
        return false;
    }

    const uint16_t bytesPerSector = readLe16(sectorBuffer + 11);
    sectorsPerCluster = sectorBuffer[13];
    reservedSectors = readLe16(sectorBuffer + 14);
    fatCount = sectorBuffer[16];
    const uint16_t rootEntryCount = readLe16(sectorBuffer + 17);
    const uint16_t total16 = readLe16(sectorBuffer + 19);
    const uint16_t fat16Size = readLe16(sectorBuffer + 22);
    const uint32_t total32 = readLe32(sectorBuffer + 32);
    fatSizeSectors = readLe32(sectorBuffer + 36);
    rootCluster = readLe32(sectorBuffer + 44) & 0x0FFFFFFFUL;
    totalVolumeSectors = total16 != 0 ? total16 : total32;

    if (bytesPerSector != SECTOR_SIZE) {
        fail("FAT32 bytes/sector must be 512");
        return false;
    }
    if (sectorsPerCluster == 0 || (sectorsPerCluster & (sectorsPerCluster - 1)) != 0) {
        fail("FAT32 sectors/cluster is invalid");
        return false;
    }
    if (reservedSectors == 0 || fatCount == 0 || fatSizeSectors == 0) {
        fail("FAT32 geometry is invalid");
        return false;
    }
    if (rootEntryCount != 0 || fat16Size != 0) {
        fail("MANTIS is not FAT32");
        return false;
    }
    if (totalVolumeSectors == 0 || totalVolumeSectors > partitionSectors) {
        fail("FAT32 volume exceeds partition");
        return false;
    }

    fatStartSector = reservedSectors;
    const uint64_t dataStart = static_cast<uint64_t>(reservedSectors) +
                               static_cast<uint64_t>(fatCount) * fatSizeSectors;
    if (dataStart >= totalVolumeSectors || dataStart > UINT32_MAX) {
        fail("FAT32 data start is invalid");
        return false;
    }
    firstDataSector = static_cast<uint32_t>(dataStart);

    const uint32_t dataSectors = totalVolumeSectors - firstDataSector;
    clusterCount = dataSectors / sectorsPerCluster;
    if (clusterCount == 0 || !validDataCluster(rootCluster)) {
        fail("FAT32 root cluster is invalid");
        return false;
    }

    ready = true;
    errorText = "OK";
    Serial.printf(
        "RAW FAT32 ready: P1=%lu sectors spc=%u root=%lu clusters=%lu\n",
        static_cast<unsigned long>(partitionSectors),
        sectorsPerCluster,
        static_cast<unsigned long>(rootCluster),
        static_cast<unsigned long>(clusterCount)
    );
    return true;
}

bool WholeSdFat32Reader::readSector(uint32_t volumeRelativeSector, uint8_t* buffer) {
    if (!storage || !buffer || volumeRelativeSector >= partitionSectors) {
        return false;
    }
    return storage->readBytes(partitionStart + volumeRelativeSector, 0, buffer, SECTOR_SIZE);
}

bool WholeSdFat32Reader::validDataCluster(uint32_t cluster) const {
    return cluster >= 2 && cluster < clusterCount + 2 && cluster < FAT32_BAD_CLUSTER;
}

uint32_t WholeSdFat32Reader::clusterFirstSector(uint32_t cluster) const {
    return firstDataSector + (cluster - 2) * sectorsPerCluster;
}

bool WholeSdFat32Reader::nextCluster(uint32_t cluster, uint32_t& next) {
    if (!validDataCluster(cluster)) {
        return false;
    }

    const uint32_t fatOffset = cluster * 4UL;
    const uint32_t fatSector = fatStartSector + fatOffset / SECTOR_SIZE;
    const uint16_t entryOffset = static_cast<uint16_t>(fatOffset % SECTOR_SIZE);

    if (!readSector(fatSector, sectorBuffer)) {
        return false;
    }

    next = readLe32(sectorBuffer + entryOffset) & 0x0FFFFFFFUL;
    return true;
}

String WholeSdFat32Reader::decodeShortName(const uint8_t* entry) {
    String base;
    String ext;

    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t c = entry[i];
        if (c == ' ') break;
        base += static_cast<char>(c == 0x05 ? 0xE5 : c);
    }
    for (uint8_t i = 8; i < 11; ++i) {
        const uint8_t c = entry[i];
        if (c == ' ') break;
        ext += static_cast<char>(c);
    }

    if (ext.length() > 0) {
        base += '.';
        base += ext;
    }
    return base;
}

String WholeSdFat32Reader::decodeLfnPart(const uint8_t* entry) {
    static const uint8_t offsets[13] = {
        1, 3, 5, 7, 9,
        14, 16, 18, 20, 22, 24,
        28, 30
    };

    String result;
    result.reserve(13);
    for (uint8_t i = 0; i < 13; ++i) {
        const uint16_t code = readLe16(entry + offsets[i]);
        if (code == 0x0000 || code == 0xFFFF) {
            break;
        }
        if (code >= 0x20 && code <= 0x7E) {
            result += static_cast<char>(code);
        } else if (code <= 0x00FF) {
            result += static_cast<char>(code);
        } else {
            result += '?';
        }
    }
    return result;
}

void WholeSdFat32Reader::resetLfn(String parts[MAX_LFN_PARTS], uint8_t& maxPart, bool& active) {
    for (uint8_t i = 0; i < MAX_LFN_PARTS; ++i) {
        parts[i] = "";
    }
    maxPart = 0;
    active = false;
}

String WholeSdFat32Reader::buildLfn(String parts[MAX_LFN_PARTS], uint8_t maxPart) {
    String result;
    for (uint8_t i = 0; i < maxPart && i < MAX_LFN_PARTS; ++i) {
        result += parts[i];
    }
    return result;
}

bool WholeSdFat32Reader::scanDirectory(
    uint32_t directoryCluster,
    std::vector<WholeSdFileEntry>* entries,
    size_t maxEntries,
    const String* matchName,
    WholeSdFileEntry* matchedEntry
) {
    if (!ready || !validDataCluster(directoryCluster)) {
        return false;
    }

    String lfnParts[MAX_LFN_PARTS];
    uint8_t lfnMaxPart = 0;
    bool lfnActive = false;
    resetLfn(lfnParts, lfnMaxPart, lfnActive);

    uint32_t cluster = directoryCluster;
    uint32_t visitedClusters = 0;

    while (validDataCluster(cluster) && visitedClusters <= clusterCount) {
        const uint32_t firstSector = clusterFirstSector(cluster);

        for (uint8_t sectorIndex = 0; sectorIndex < sectorsPerCluster; ++sectorIndex) {
            if (!readSector(firstSector + sectorIndex, sectorBuffer)) {
                return false;
            }

            for (uint16_t offset = 0; offset < SECTOR_SIZE; offset += 32) {
                const uint8_t* entry = sectorBuffer + offset;
                const uint8_t firstByte = entry[0];

                if (firstByte == 0x00) {
                    return matchName == nullptr;
                }
                if (firstByte == 0xE5) {
                    resetLfn(lfnParts, lfnMaxPart, lfnActive);
                    continue;
                }

                const uint8_t attributes = entry[11];
                if (attributes == 0x0F) {
                    const uint8_t sequence = entry[0];
                    const uint8_t partNumber = sequence & 0x1F;
                    if (partNumber == 0 || partNumber > MAX_LFN_PARTS) {
                        resetLfn(lfnParts, lfnMaxPart, lfnActive);
                        continue;
                    }
                    if (sequence & 0x40) {
                        resetLfn(lfnParts, lfnMaxPart, lfnActive);
                        lfnMaxPart = partNumber;
                        lfnActive = true;
                    }
                    if (lfnActive) {
                        lfnParts[partNumber - 1] = decodeLfnPart(entry);
                        if (partNumber > lfnMaxPart) lfnMaxPart = partNumber;
                    }
                    continue;
                }

                String entryName = lfnActive ? buildLfn(lfnParts, lfnMaxPart) : decodeShortName(entry);
                resetLfn(lfnParts, lfnMaxPart, lfnActive);

                if ((attributes & 0x08) != 0 || entryName.length() == 0 ||
                    entryName == "." || entryName == "..") {
                    continue;
                }

                WholeSdFileEntry fileEntry;
                fileEntry.name = entryName;
                fileEntry.isDir = (attributes & 0x10) != 0;
                fileEntry.firstCluster =
                    (static_cast<uint32_t>(readLe16(entry + 20)) << 16) |
                    readLe16(entry + 26);
                fileEntry.size = readLe32(entry + 28);

                if (matchName && entryName.equalsIgnoreCase(*matchName)) {
                    if (matchedEntry) {
                        *matchedEntry = fileEntry;
                    }
                    return true;
                }

                if (entries && entries->size() < maxEntries) {
                    entries->push_back(fileEntry);
                }
            }
            delay(1);
        }

        uint32_t next = 0;
        if (!nextCluster(cluster, next)) {
            return false;
        }
        if (next >= FAT32_EOC_MIN) {
            return matchName == nullptr;
        }
        if (next == FAT32_BAD_CLUSTER || next == 0 || next == 1 || next == cluster) {
            return false;
        }

        cluster = next;
        visitedClusters++;
        delay(1);
    }

    return matchName == nullptr;
}

bool WholeSdFat32Reader::findInDirectory(uint32_t directoryCluster, const String& name, WholeSdFileEntry& entry) {
    return scanDirectory(directoryCluster, nullptr, 0, &name, &entry);
}

bool WholeSdFat32Reader::resolveDirectory(const String& path, uint32_t& directoryCluster) {
    if (!ready) {
        return false;
    }

    directoryCluster = rootCluster;
    if (path.length() == 0 || path == "/") {
        return true;
    }

    int index = 0;
    while (index < static_cast<int>(path.length())) {
        while (index < static_cast<int>(path.length()) && path[index] == '/') index++;
        if (index >= static_cast<int>(path.length())) break;

        int end = path.indexOf('/', index);
        if (end < 0) end = path.length();
        const String segment = path.substring(index, end);
        index = end + 1;

        if (segment.length() == 0 || segment == ".") continue;
        if (segment == "..") return false;

        WholeSdFileEntry entry;
        if (!findInDirectory(directoryCluster, segment, entry) || !entry.isDir ||
            !validDataCluster(entry.firstCluster)) {
            return false;
        }
        directoryCluster = entry.firstCluster;
    }

    return true;
}

bool WholeSdFat32Reader::resolveEntry(const String& path, WholeSdFileEntry& entry) {
    if (!ready || path.length() == 0 || path == "/") {
        return false;
    }

    const int slash = path.lastIndexOf('/');
    const String parent = slash <= 0 ? "/" : path.substring(0, slash);
    const String name = path.substring(slash + 1);
    if (name.length() == 0) {
        return false;
    }

    uint32_t directoryCluster = 0;
    if (!resolveDirectory(parent, directoryCluster)) {
        return false;
    }
    return findInDirectory(directoryCluster, name, entry);
}

bool WholeSdFat32Reader::directoryExists(const String& path) {
    uint32_t cluster = 0;
    return resolveDirectory(path, cluster);
}

bool WholeSdFat32Reader::listDirectory(const String& path, std::vector<WholeSdFileEntry>& entries, size_t maxEntries) {
    entries.clear();

    uint32_t directoryCluster = 0;
    if (!resolveDirectory(path, directoryCluster)) {
        return false;
    }

    return scanDirectory(directoryCluster, &entries, maxEntries, nullptr, nullptr);
}

bool WholeSdFat32Reader::readFile(const String& path, String& output, size_t maxBytes) {
    output = "";

    WholeSdFileEntry entry;
    if (!resolveEntry(path, entry) || entry.isDir) {
        return false;
    }
    if (entry.size > maxBytes) {
        return false;
    }
    if (entry.size == 0) {
        return true;
    }
    if (!validDataCluster(entry.firstCluster)) {
        return false;
    }

    output.reserve(entry.size + 1);
    uint32_t remaining = entry.size;
    uint32_t cluster = entry.firstCluster;
    uint32_t visitedClusters = 0;

    while (remaining > 0 && validDataCluster(cluster) && visitedClusters <= clusterCount) {
        const uint32_t firstSector = clusterFirstSector(cluster);
        for (uint8_t sectorIndex = 0; sectorIndex < sectorsPerCluster && remaining > 0; ++sectorIndex) {
            if (!readSector(firstSector + sectorIndex, sectorBuffer)) {
                output = "";
                return false;
            }

            const uint16_t chunk = remaining > SECTOR_SIZE ? SECTOR_SIZE : static_cast<uint16_t>(remaining);
            for (uint16_t i = 0; i < chunk; ++i) {
                output += static_cast<char>(sectorBuffer[i]);
            }
            remaining -= chunk;
            delay(1);
        }

        if (remaining == 0) {
            return true;
        }

        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= FAT32_EOC_MIN ||
            next == FAT32_BAD_CLUSTER || next == 0 || next == 1 || next == cluster) {
            output = "";
            return false;
        }

        cluster = next;
        visitedClusters++;
        delay(1);
    }

    if (remaining != 0) {
        output = "";
        return false;
    }
    return true;
}

bool WholeSdFat32Reader::fileSize(const String& path, uint32_t& size) {
    size = 0;
    WholeSdFileEntry entry;
    if (!resolveEntry(path, entry) || entry.isDir) {
        return false;
    }
    size = entry.size;
    return true;
}

bool WholeSdFat32Reader::readFileRange(
    const String& path,
    uint32_t offset,
    uint8_t* buffer,
    size_t length
) {
    if (!buffer && length > 0) {
        return false;
    }
    if (length == 0) {
        return true;
    }

    WholeSdFileEntry entry;
    if (!resolveEntry(path, entry) || entry.isDir) {
        return false;
    }
    if (offset > entry.size || length > static_cast<size_t>(entry.size - offset)) {
        return false;
    }
    if (!validDataCluster(entry.firstCluster)) {
        return false;
    }

    const uint32_t clusterBytes = static_cast<uint32_t>(sectorsPerCluster) * SECTOR_SIZE;
    uint32_t clusterSkip = offset / clusterBytes;
    uint32_t offsetInCluster = offset % clusterBytes;
    uint32_t cluster = entry.firstCluster;

    for (uint32_t i = 0; i < clusterSkip; ++i) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= FAT32_EOC_MIN ||
            next == FAT32_BAD_CLUSTER || next == 0 || next == 1 || next == cluster) {
            return false;
        }
        cluster = next;
    }

    size_t copied = 0;
    uint32_t visitedClusters = 0;
    while (copied < length && validDataCluster(cluster) && visitedClusters <= clusterCount) {
        const uint32_t firstSector = clusterFirstSector(cluster);
        uint32_t localOffset = offsetInCluster;

        for (uint8_t sectorIndex = 0; sectorIndex < sectorsPerCluster && copied < length; ++sectorIndex) {
            if (localOffset >= SECTOR_SIZE) {
                localOffset -= SECTOR_SIZE;
                continue;
            }

            if (!readSector(firstSector + sectorIndex, sectorBuffer)) {
                return false;
            }

            const uint16_t sectorOffset = static_cast<uint16_t>(localOffset);
            const size_t available = SECTOR_SIZE - sectorOffset;
            const size_t remaining = length - copied;
            const size_t chunk = available < remaining ? available : remaining;
            memcpy(buffer + copied, sectorBuffer + sectorOffset, chunk);
            copied += chunk;
            localOffset = 0;
        }

        offsetInCluster = 0;
        if (copied >= length) {
            return true;
        }

        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= FAT32_EOC_MIN ||
            next == FAT32_BAD_CLUSTER || next == 0 || next == 1 || next == cluster) {
            return false;
        }
        cluster = next;
        visitedClusters++;
    }

    return copied == length;
}

bool WholeSdFat32Reader::isReady() const {
    return ready;
}

const char* WholeSdFat32Reader::lastError() const {
    return errorText;
}
