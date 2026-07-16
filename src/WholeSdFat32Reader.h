#ifndef MANTIS_WHOLE_SD_FAT32_READER_H
#define MANTIS_WHOLE_SD_FAT32_READER_H

#include <Arduino.h>
#include <vector>
#include "SDWholeCardStorage.h"

struct WholeSdFileEntry {
    String name;
    bool isDir;
    uint32_t firstCluster;
    uint32_t size;
};

class WholeSdFat32Reader {
public:
    WholeSdFat32Reader();

    bool begin(SDWholeCardStorage& storage);
    bool isReady() const;
    const char* lastError() const;

    bool directoryExists(const String& path);
    bool listDirectory(const String& path, std::vector<WholeSdFileEntry>& entries, size_t maxEntries = 100);
    bool readFile(const String& path, String& output, size_t maxBytes = 20000);
    bool fileSize(const String& path, uint32_t& size);
    bool readFileRange(const String& path, uint32_t offset, uint8_t* buffer, size_t length);

private:
    static constexpr uint16_t SECTOR_SIZE = 512;
    static constexpr uint32_t FAT32_EOC_MIN = 0x0FFFFFF8UL;
    static constexpr uint32_t FAT32_BAD_CLUSTER = 0x0FFFFFF7UL;
    static constexpr uint8_t MAX_LFN_PARTS = 20;

    SDWholeCardStorage* storage;
    bool ready;
    const char* errorText;

    uint32_t partitionStart;
    uint32_t partitionSectors;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t fatCount;
    uint32_t fatSizeSectors;
    uint32_t totalVolumeSectors;
    uint32_t fatStartSector;
    uint32_t firstDataSector;
    uint32_t rootCluster;
    uint32_t clusterCount;

    alignas(4) uint8_t sectorBuffer[SECTOR_SIZE];

    void fail(const char* message);
    bool readSector(uint32_t volumeRelativeSector, uint8_t* buffer);
    bool validDataCluster(uint32_t cluster) const;
    uint32_t clusterFirstSector(uint32_t cluster) const;
    bool nextCluster(uint32_t cluster, uint32_t& next);

    bool resolveDirectory(const String& path, uint32_t& directoryCluster);
    bool resolveEntry(const String& path, WholeSdFileEntry& entry);
    bool findInDirectory(uint32_t directoryCluster, const String& name, WholeSdFileEntry& entry);
    bool scanDirectory(
        uint32_t directoryCluster,
        std::vector<WholeSdFileEntry>* entries,
        size_t maxEntries,
        const String* matchName,
        WholeSdFileEntry* matchedEntry
    );

    static uint16_t readLe16(const uint8_t* p);
    static uint32_t readLe32(const uint8_t* p);
    static String decodeShortName(const uint8_t* entry);
    static String decodeLfnPart(const uint8_t* entry);
    static void resetLfn(String parts[MAX_LFN_PARTS], uint8_t& maxPart, bool& active);
    static String buildLfn(String parts[MAX_LFN_PARTS], uint8_t maxPart);
};

#endif
