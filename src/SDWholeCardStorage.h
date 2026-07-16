#ifndef MANTIS_SD_WHOLE_CARD_STORAGE_H
#define MANTIS_SD_WHOLE_CARD_STORAGE_H

#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "target_config.h"

class SDWholeCardStorage {
public:
    SDWholeCardStorage();
    ~SDWholeCardStorage();

    bool begin();

    bool readBytes(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    bool writeBytes(uint32_t lba, uint32_t offset, const uint8_t* buffer, uint32_t bufsize);
    bool sync();
    void service();

    bool isReady() const;
    uint32_t sectorCount() const;
    uint16_t sectorSize() const;
    uint64_t capacityBytes() const;
    uint32_t partitionStartLba() const;
    uint32_t partitionSectorCount() const;
    uint8_t partitionType() const;
    uint8_t driveNumber() const;
    const char* lastError() const;

    uint64_t physicalCapacityBytes() const;
    uint32_t imageClusterCount() const;

private:
    static constexpr uint16_t SECTOR_SIZE = 512;
    static constexpr int SD_FREQUENCY_HZ = MANTIS_SD_FREQUENCY_HZ;
    static constexpr uint8_t SD_SCK = MANTIS_SD_SCK;
    static constexpr uint8_t SD_MISO = MANTIS_SD_MISO;
    static constexpr uint8_t SD_MOSI = MANTIS_SD_MOSI;
    static constexpr uint8_t SD_CS = MANTIS_SD_CS;

    static constexpr uint64_t MIN_IMAGE_BYTES = 64ULL * 1024ULL * 1024ULL;
    static constexpr uint64_t MAX_IMAGE_BYTES = 0xFFFFFE00ULL;
    static constexpr uint32_t FAT32_EOC_MIN = 0x0FFFFFF8UL;
    static constexpr uint32_t FAT32_BAD_CLUSTER = 0x0FFFFFF7UL;

    uint8_t pdrv;
    bool ready;

    uint32_t totalSectors;
    uint32_t p1Start;
    uint32_t p1Sectors;
    uint8_t p1Type;

    uint32_t physicalSectors;
    uint32_t outerPartitionStart;
    uint32_t outerPartitionSectors;
    uint8_t outerPartitionType;
    uint8_t outerSectorsPerCluster;
    uint16_t outerReservedSectors;
    uint8_t outerFatCount;
    uint32_t outerFatSizeSectors;
    uint32_t outerFirstDataSector;
    uint32_t outerRootCluster;
    uint32_t outerClusterCount;

    uint32_t imageFirstCluster;
    uint32_t imageFileBytes;
    uint32_t* imageClusters;
    uint32_t imageClustersUsed;

    SemaphoreHandle_t ioMutex;
    const char* errorText;
    alignas(4) uint8_t scratch[SECTOR_SIZE];

    bool lock(TickType_t timeout = pdMS_TO_TICKS(250));
    void unlock();
    void fail(const char* message);
    void releaseClusterMap();

    bool readPhysicalSectors(uint32_t cardLba, uint8_t* buffer, uint32_t count);
    bool writePhysicalSectors(uint32_t cardLba, const uint8_t* buffer, uint32_t count);

    bool parseOuterFat32();
    bool findImageFile();
    bool loadImageClusterMap();
    bool outerValidDataCluster(uint32_t cluster) const;
    uint32_t outerClusterFirstPhysicalSector(uint32_t cluster) const;
    bool outerNextCluster(uint32_t cluster, uint32_t& next);

    bool mapImageSectorRun(
        uint32_t imageLba,
        uint32_t requestedSectors,
        uint32_t& physicalLba,
        uint32_t& contiguousSectors
    ) const;
    bool readImageSectors(uint32_t imageLba, uint8_t* buffer, uint32_t count);
    bool writeImageSectors(uint32_t imageLba, const uint8_t* buffer, uint32_t count);
    bool parseVirtualMbr();

    static uint16_t readLe16(const uint8_t* p);
    static uint32_t readLe32(const uint8_t* p);
};

#endif
