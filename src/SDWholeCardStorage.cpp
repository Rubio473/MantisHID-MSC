#include "SDWholeCardStorage.h"

#include "sd_diskio.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

extern "C" {
#include "ff.h"
#include "diskio.h"
}

namespace {
bool isMantisUsbImageShortEntry(const uint8_t* entry) {

    return entry != nullptr &&
           memcmp(entry, "MANTIS", 6) == 0 &&
           entry[8] == 'I' && entry[9] == 'M' && entry[10] == 'G';
}
}

SDWholeCardStorage::SDWholeCardStorage()
    : pdrv(0xFF),
      ready(false),
      totalSectors(0),
      p1Start(0),
      p1Sectors(0),
      p1Type(0),
      physicalSectors(0),
      outerPartitionStart(0),
      outerPartitionSectors(0),
      outerPartitionType(0),
      outerSectorsPerCluster(0),
      outerReservedSectors(0),
      outerFatCount(0),
      outerFatSizeSectors(0),
      outerFirstDataSector(0),
      outerRootCluster(0),
      outerClusterCount(0),
      imageFirstCluster(0),
      imageFileBytes(0),
      imageClusters(nullptr),
      imageClustersUsed(0),
      ioMutex(nullptr),
      errorText("not initialized") {
    memset(scratch, 0, sizeof(scratch));
}

SDWholeCardStorage::~SDWholeCardStorage() {
    releaseClusterMap();
    if (ioMutex) {
        vSemaphoreDelete(ioMutex);
        ioMutex = nullptr;
    }
}

bool SDWholeCardStorage::lock(TickType_t timeout) {
    return ioMutex && xSemaphoreTake(ioMutex, timeout) == pdTRUE;
}

void SDWholeCardStorage::unlock() {
    if (ioMutex) {
        xSemaphoreGive(ioMutex);
    }
}

void SDWholeCardStorage::fail(const char* message) {
    ready = false;
    errorText = message;
    Serial.printf("SDIMAGE ERROR: %s\n", message);
}

void SDWholeCardStorage::releaseClusterMap() {
    if (imageClusters) {
        heap_caps_free(imageClusters);
        imageClusters = nullptr;
    }
    imageClustersUsed = 0;
}

uint16_t SDWholeCardStorage::readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t SDWholeCardStorage::readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool SDWholeCardStorage::readPhysicalSectors(uint32_t cardLba, uint8_t* buffer, uint32_t count) {
    if (!buffer || count == 0) {
        return count == 0;
    }
    if (cardLba >= physicalSectors || count > physicalSectors - cardLba) {
        return false;
    }
    return disk_read(
               pdrv,
               buffer,
               static_cast<LBA_t>(cardLba),
               static_cast<UINT>(count)
           ) == RES_OK;
}

bool SDWholeCardStorage::writePhysicalSectors(
    uint32_t cardLba,
    const uint8_t* buffer,
    uint32_t count
) {
    if (!buffer || count == 0) {
        return count == 0;
    }
    if (cardLba >= physicalSectors || count > physicalSectors - cardLba) {
        return false;
    }
    return disk_write(
               pdrv,
               const_cast<BYTE*>(reinterpret_cast<const BYTE*>(buffer)),
               static_cast<LBA_t>(cardLba),
               static_cast<UINT>(count)
           ) == RES_OK;
}

bool SDWholeCardStorage::outerValidDataCluster(uint32_t cluster) const {
    return cluster >= 2 &&
           cluster < outerClusterCount + 2 &&
           cluster < FAT32_BAD_CLUSTER;
}

uint32_t SDWholeCardStorage::outerClusterFirstPhysicalSector(uint32_t cluster) const {
    const uint32_t relative =
        outerFirstDataSector + (cluster - 2) * outerSectorsPerCluster;
    return outerPartitionStart + relative;
}

bool SDWholeCardStorage::outerNextCluster(uint32_t cluster, uint32_t& next) {
    if (!outerValidDataCluster(cluster)) {
        return false;
    }

    const uint32_t fatOffset = cluster * 4UL;
    const uint32_t fatSector =
        outerPartitionStart + outerReservedSectors + fatOffset / SECTOR_SIZE;
    const uint16_t entryOffset = static_cast<uint16_t>(fatOffset % SECTOR_SIZE);

    if (!readPhysicalSectors(fatSector, scratch, 1)) {
        return false;
    }

    next = readLe32(scratch + entryOffset) & 0x0FFFFFFFUL;
    return true;
}

bool SDWholeCardStorage::parseOuterFat32() {
    if (!readPhysicalSectors(0, scratch, 1)) {
        fail("physical MBR read failed");
        return false;
    }
    if (scratch[510] != 0x55 || scratch[511] != 0xAA) {
        fail("physical SD requires an MBR partition table");
        return false;
    }

    const uint8_t* p1 = scratch + 446;
    outerPartitionType = p1[4];
    outerPartitionStart = readLe32(p1 + 8);
    outerPartitionSectors = readLe32(p1 + 12);
    if (outerPartitionType == 0 || outerPartitionStart == 0 || outerPartitionSectors == 0) {
        fail("physical SD partition 1 is missing");
        return false;
    }
    if (static_cast<uint64_t>(outerPartitionStart) + outerPartitionSectors > physicalSectors) {
        fail("physical SD partition exceeds card capacity");
        return false;
    }

    for (int index = 1; index < 4; ++index) {
        const uint8_t* entry = scratch + 446 + index * 16;
        if (entry[4] != 0 || readLe32(entry + 8) != 0 || readLe32(entry + 12) != 0) {
            fail("physical SD must contain only one partition");
            return false;
        }
    }

    if (!readPhysicalSectors(outerPartitionStart, scratch, 1)) {
        fail("physical FAT32 boot sector read failed");
        return false;
    }
    if (scratch[510] != 0x55 || scratch[511] != 0xAA) {
        fail("physical FAT32 boot signature is invalid");
        return false;
    }

    const uint16_t bytesPerSector = readLe16(scratch + 11);
    outerSectorsPerCluster = scratch[13];
    outerReservedSectors = readLe16(scratch + 14);
    outerFatCount = scratch[16];
    const uint16_t rootEntryCount = readLe16(scratch + 17);
    const uint16_t total16 = readLe16(scratch + 19);
    const uint16_t fat16Size = readLe16(scratch + 22);
    const uint32_t total32 = readLe32(scratch + 32);
    outerFatSizeSectors = readLe32(scratch + 36);
    outerRootCluster = readLe32(scratch + 44) & 0x0FFFFFFFUL;
    const uint32_t volumeSectors = total16 != 0 ? total16 : total32;

    if (bytesPerSector != SECTOR_SIZE) {
        fail("physical FAT32 bytes/sector must be 512");
        return false;
    }
    if (outerSectorsPerCluster == 0 ||
        (outerSectorsPerCluster & (outerSectorsPerCluster - 1)) != 0) {
        fail("physical FAT32 sectors/cluster is invalid");
        return false;
    }
    if (outerReservedSectors == 0 || outerFatCount == 0 || outerFatSizeSectors == 0) {
        fail("physical FAT32 geometry is invalid");
        return false;
    }
    if (rootEntryCount != 0 || fat16Size != 0) {
        fail("physical SD partition must be FAT32");
        return false;
    }
    if (volumeSectors == 0 || volumeSectors > outerPartitionSectors) {
        fail("physical FAT32 volume exceeds partition");
        return false;
    }

    const uint64_t firstData = static_cast<uint64_t>(outerReservedSectors) +
                               static_cast<uint64_t>(outerFatCount) * outerFatSizeSectors;
    if (firstData >= volumeSectors || firstData > UINT32_MAX) {
        fail("physical FAT32 data start is invalid");
        return false;
    }
    outerFirstDataSector = static_cast<uint32_t>(firstData);
    outerClusterCount = (volumeSectors - outerFirstDataSector) / outerSectorsPerCluster;
    if (outerClusterCount == 0 || !outerValidDataCluster(outerRootCluster)) {
        fail("physical FAT32 root cluster is invalid");
        return false;
    }

    return true;
}

bool SDWholeCardStorage::findImageFile() {
    uint32_t cluster = outerRootCluster;
    uint32_t visited = 0;

    while (outerValidDataCluster(cluster) && visited <= outerClusterCount) {
        const uint32_t firstSector = outerClusterFirstPhysicalSector(cluster);
        for (uint8_t sectorIndex = 0; sectorIndex < outerSectorsPerCluster; ++sectorIndex) {
            if (!readPhysicalSectors(firstSector + sectorIndex, scratch, 1)) {
                fail("physical root directory read failed");
                return false;
            }

            for (uint16_t offset = 0; offset < SECTOR_SIZE; offset += 32) {
                const uint8_t* entry = scratch + offset;
                if (entry[0] == 0x00) {
                    fail("MANTISUSB.IMG not found in physical SD root");
                    return false;
                }
                if (entry[0] == 0xE5 || entry[11] == 0x0F || (entry[11] & 0x08) != 0) {
                    continue;
                }
                if (!isMantisUsbImageShortEntry(entry)) {
                    continue;
                }
                if ((entry[11] & 0x10) != 0) {
                    fail("MANTISUSB.IMG is a directory");
                    return false;
                }

                imageFirstCluster =
                    (static_cast<uint32_t>(readLe16(entry + 20)) << 16) |
                    readLe16(entry + 26);
                imageFileBytes = readLe32(entry + 28);

                const uint64_t detectedBytes = imageFileBytes;
                if (detectedBytes < MIN_IMAGE_BYTES || detectedBytes > MAX_IMAGE_BYTES ||
                    (detectedBytes % SECTOR_SIZE) != 0) {
                    fail("MANTISUSB.IMG size must be 64 MiB to 4 GiB and sector aligned");
                    return false;
                }
                if (!outerValidDataCluster(imageFirstCluster)) {
                    fail("MANTISUSB.IMG first cluster is invalid");
                    return false;
                }
                return true;
            }
        }

        uint32_t next = 0;
        if (!outerNextCluster(cluster, next)) {
            fail("physical root FAT chain read failed");
            return false;
        }
        if (next >= FAT32_EOC_MIN) {
            fail("MANTISUSB.IMG not found in physical SD root");
            return false;
        }
        if (!outerValidDataCluster(next) || next == cluster) {
            fail("physical root FAT chain is corrupt");
            return false;
        }
        cluster = next;
        ++visited;
    }

    fail("physical root directory traversal failed");
    return false;
}

bool SDWholeCardStorage::loadImageClusterMap() {
    releaseClusterMap();

    const uint32_t clusterBytes =
        static_cast<uint32_t>(outerSectorsPerCluster) * SECTOR_SIZE;
    const uint32_t required =
        static_cast<uint32_t>((static_cast<uint64_t>(imageFileBytes) + clusterBytes - 1) /
                              clusterBytes);
    if (required == 0 || required > outerClusterCount) {
        fail("MANTISUSB.IMG cluster count is invalid");
        return false;
    }

    const size_t bytes = static_cast<size_t>(required) * sizeof(uint32_t);
    imageClusters = static_cast<uint32_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (!imageClusters) {
        imageClusters = static_cast<uint32_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    if (!imageClusters) {
        fail("cannot allocate MANTISUSB.IMG cluster map");
        return false;
    }

    uint32_t cluster = imageFirstCluster;
    for (uint32_t index = 0; index < required; ++index) {
        if (!outerValidDataCluster(cluster)) {
            fail("MANTISUSB.IMG FAT chain ended early");
            releaseClusterMap();
            return false;
        }
        imageClusters[index] = cluster;

        if (index + 1 < required) {
            uint32_t next = 0;
            if (!outerNextCluster(cluster, next) || next >= FAT32_EOC_MIN ||
                next == cluster || !outerValidDataCluster(next)) {
                fail("MANTISUSB.IMG FAT chain is incomplete/corrupt");
                releaseClusterMap();
                return false;
            }
            cluster = next;
        }
    }

    imageClustersUsed = required;
    return true;
}

bool SDWholeCardStorage::mapImageSectorRun(
    uint32_t imageLba,
    uint32_t requestedSectors,
    uint32_t& physicalLba,
    uint32_t& contiguousSectors
) const {
    physicalLba = 0;
    contiguousSectors = 0;
    if (!imageClusters || requestedSectors == 0 || imageLba >= totalSectors ||
        requestedSectors > totalSectors - imageLba) {
        return false;
    }

    const uint32_t clusterIndex = imageLba / outerSectorsPerCluster;
    const uint32_t sectorInCluster = imageLba % outerSectorsPerCluster;
    if (clusterIndex >= imageClustersUsed) {
        return false;
    }

    const uint32_t cluster = imageClusters[clusterIndex];
    physicalLba = outerClusterFirstPhysicalSector(cluster) + sectorInCluster;
    contiguousSectors = std::min(
        requestedSectors,
        static_cast<uint32_t>(outerSectorsPerCluster - sectorInCluster)
    );

    uint32_t mapIndex = clusterIndex;
    uint32_t lastCluster = cluster;
    while (contiguousSectors < requestedSectors && mapIndex + 1 < imageClustersUsed) {
        const uint32_t nextCluster = imageClusters[mapIndex + 1];
        if (nextCluster != lastCluster + 1) {
            break;
        }
        const uint32_t add = std::min(
            requestedSectors - contiguousSectors,
            static_cast<uint32_t>(outerSectorsPerCluster)
        );
        contiguousSectors += add;
        ++mapIndex;
        lastCluster = nextCluster;
        if (add < outerSectorsPerCluster) {
            break;
        }
    }

    return contiguousSectors > 0;
}

bool SDWholeCardStorage::readImageSectors(
    uint32_t imageLba,
    uint8_t* buffer,
    uint32_t count
) {
    if (count == 0) {
        return true;
    }
    if (!buffer || imageLba >= totalSectors || count > totalSectors - imageLba) {
        return false;
    }

    uint32_t remaining = count;
    uint32_t current = imageLba;
    uint8_t* out = buffer;
    while (remaining > 0) {
        uint32_t physical = 0;
        uint32_t run = 0;
        if (!mapImageSectorRun(current, remaining, physical, run) ||
            !readPhysicalSectors(physical, out, run)) {
            return false;
        }
        current += run;
        remaining -= run;
        out += run * SECTOR_SIZE;
    }
    return true;
}

bool SDWholeCardStorage::writeImageSectors(
    uint32_t imageLba,
    const uint8_t* buffer,
    uint32_t count
) {
    if (count == 0) {
        return true;
    }
    if (!buffer || imageLba >= totalSectors || count > totalSectors - imageLba) {
        return false;
    }

    uint32_t remaining = count;
    uint32_t current = imageLba;
    const uint8_t* in = buffer;
    while (remaining > 0) {
        uint32_t physical = 0;
        uint32_t run = 0;
        if (!mapImageSectorRun(current, remaining, physical, run) ||
            !writePhysicalSectors(physical, in, run)) {
            return false;
        }
        current += run;
        remaining -= run;
        in += run * SECTOR_SIZE;
    }
    return true;
}

bool SDWholeCardStorage::parseVirtualMbr() {
    if (!readImageSectors(0, scratch, 1)) {
        fail("MANTISUSB.IMG MBR read failed");
        return false;
    }
    if (scratch[510] != 0x55 || scratch[511] != 0xAA) {
        fail("MANTISUSB.IMG has no valid MBR signature");
        return false;
    }

    const uint8_t* p1 = scratch + 446;
    p1Type = p1[4];
    p1Start = readLe32(p1 + 8);
    p1Sectors = readLe32(p1 + 12);
    if (p1Type == 0 || p1Start == 0 || p1Sectors == 0) {
        fail("MANTISUSB.IMG partition 1 is missing");
        return false;
    }
    if (static_cast<uint64_t>(p1Start) + p1Sectors > totalSectors) {
        fail("MANTISUSB.IMG partition exceeds image capacity");
        return false;
    }

    for (int index = 1; index < 4; ++index) {
        const uint8_t* entry = scratch + 446 + index * 16;
        if (entry[4] != 0 || readLe32(entry + 8) != 0 || readLe32(entry + 12) != 0) {
            fail("MANTISUSB.IMG must contain only one partition");
            return false;
        }
    }
    if (p1Type != 0x0B && p1Type != 0x0C) {
        fail("MANTISUSB.IMG partition must be FAT32");
        return false;
    }
    if (!readImageSectors(p1Start, scratch, 1)) {
        fail("MANTISUSB.IMG FAT32 boot sector read failed");
        return false;
    }
    if (scratch[510] != 0x55 || scratch[511] != 0xAA) {
        fail("MANTISUSB.IMG FAT32 boot signature is invalid");
        return false;
    }
    const uint16_t bytesPerSector = readLe16(scratch + 11);
    const uint8_t sectorsPerCluster = scratch[13];
    const uint16_t reservedSectors = readLe16(scratch + 14);
    const uint8_t fatCount = scratch[16];
    const uint16_t rootEntryCount = readLe16(scratch + 17);
    const uint16_t total16 = readLe16(scratch + 19);
    const uint16_t fat16Size = readLe16(scratch + 22);
    const uint32_t total32 = readLe32(scratch + 32);
    const uint32_t fat32Size = readLe32(scratch + 36);
    const uint32_t rootCluster = readLe32(scratch + 44) & 0x0FFFFFFFUL;
    const uint32_t volumeSectors = total16 != 0 ? total16 : total32;
    if (bytesPerSector != SECTOR_SIZE || sectorsPerCluster == 0 ||
        (sectorsPerCluster & (sectorsPerCluster - 1)) != 0 ||
        reservedSectors == 0 || fatCount == 0 || rootEntryCount != 0 ||
        fat16Size != 0 || fat32Size == 0 || rootCluster < 2) {
        fail("MANTISUSB.IMG FAT32 geometry is invalid");
        return false;
    }
    if (volumeSectors == 0 || volumeSectors > p1Sectors) {
        fail("MANTISUSB.IMG FAT32 volume exceeds partition");
        return false;
    }
    const uint64_t firstData = static_cast<uint64_t>(reservedSectors) +
                               static_cast<uint64_t>(fatCount) * fat32Size;
    if (firstData >= volumeSectors) {
        fail("MANTISUSB.IMG FAT32 data region is invalid");
        return false;
    }
    const uint64_t clusters = (volumeSectors - firstData) / sectorsPerCluster;
    if (clusters < 65525ULL) {
        fail("MANTISUSB.IMG is not an unambiguous FAT32 volume");
        return false;
    }
    return true;
}

bool SDWholeCardStorage::begin() {
    ready = false;
    totalSectors = 0;
    p1Start = 0;
    p1Sectors = 0;
    p1Type = 0;
    physicalSectors = 0;
    outerPartitionStart = 0;
    outerPartitionSectors = 0;
    outerPartitionType = 0;
    outerSectorsPerCluster = 0;
    outerReservedSectors = 0;
    outerFatCount = 0;
    outerFatSizeSectors = 0;
    outerFirstDataSector = 0;
    outerRootCluster = 0;
    outerClusterCount = 0;
    imageFirstCluster = 0;
    imageFileBytes = 0;
    releaseClusterMap();
    errorText = "initializing";

    if (!ioMutex) {
        ioMutex = xSemaphoreCreateMutex();
        if (!ioMutex) {
            fail("mutex allocation failed");
            return false;
        }
    }

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    pdrv = sdcard_init(SD_CS, &SPI, SD_FREQUENCY_HZ);
    if (pdrv == 0xFF) {
        fail("sdcard_init failed");
        return false;
    }

    const DSTATUS status = disk_initialize(pdrv);
    if (status & STA_NOINIT) {
        fail("disk_initialize returned STA_NOINIT");
        return false;
    }

    LBA_t cardSectors = 0;
    WORD cardSectorSize = 0;
    if (disk_ioctl(pdrv, GET_SECTOR_COUNT, &cardSectors) != RES_OK) {
        fail("GET_SECTOR_COUNT failed");
        return false;
    }
    if (disk_ioctl(pdrv, GET_SECTOR_SIZE, &cardSectorSize) != RES_OK) {
        fail("GET_SECTOR_SIZE failed");
        return false;
    }
    if (cardSectorSize != SECTOR_SIZE || cardSectors == 0 || cardSectors > UINT32_MAX) {
        fail("physical microSD geometry is unsupported");
        return false;
    }
    physicalSectors = static_cast<uint32_t>(cardSectors);

    if (!parseOuterFat32() || !findImageFile() || !loadImageClusterMap()) {
        return false;
    }

    totalSectors = imageFileBytes / SECTOR_SIZE;
    if (!parseVirtualMbr()) {
        return false;
    }

    ready = true;
    errorText = "OK";
    Serial.printf(
        "SDIMAGE ready: target=%s physical=%llu MiB outerSPC=%u image=%llu MiB "
        "imageClusters=%lu virtualP1 type=0x%02X start=%lu sectors=%lu\n",
        MANTIS_TARGET_NAME,
        physicalCapacityBytes() / (1024ULL * 1024ULL),
        outerSectorsPerCluster,
        capacityBytes() / (1024ULL * 1024ULL),
        static_cast<unsigned long>(imageClustersUsed),
        p1Type,
        static_cast<unsigned long>(p1Start),
        static_cast<unsigned long>(p1Sectors)
    );
    return true;
}

bool SDWholeCardStorage::readBytes(
    uint32_t lba,
    uint32_t offset,
    void* buffer,
    uint32_t bufsize
) {
    if (!ready || !buffer) {
        return false;
    }

    const uint64_t firstByte = static_cast<uint64_t>(lba) * SECTOR_SIZE + offset;
    const uint64_t endByte = firstByte + bufsize;
    const uint64_t imageBytes = static_cast<uint64_t>(totalSectors) * SECTOR_SIZE;
    if (endByte > imageBytes || endByte < firstByte) {
        return false;
    }

    if (!lock()) {
        return false;
    }

    uint8_t* out = static_cast<uint8_t*>(buffer);
    uint64_t absolute = firstByte;
    uint32_t remaining = bufsize;
    bool ok = true;

    while (remaining > 0 && ok) {
        const uint32_t sector = static_cast<uint32_t>(absolute / SECTOR_SIZE);
        const uint32_t sectorOffset = static_cast<uint32_t>(absolute % SECTOR_SIZE);

        if (sectorOffset == 0 && remaining >= SECTOR_SIZE) {
            const uint32_t fullSectors = remaining / SECTOR_SIZE;
            ok = readImageSectors(sector, out, fullSectors);
            if (ok) {
                const uint32_t bytes = fullSectors * SECTOR_SIZE;
                out += bytes;
                absolute += bytes;
                remaining -= bytes;
            }
        } else {
            ok = readImageSectors(sector, scratch, 1);
            if (ok) {
                const uint32_t available = SECTOR_SIZE - sectorOffset;
                const uint32_t chunk = std::min(remaining, available);
                memcpy(out, scratch + sectorOffset, chunk);
                out += chunk;
                absolute += chunk;
                remaining -= chunk;
            }
        }
    }

    unlock();
    return ok;
}

bool SDWholeCardStorage::writeBytes(
    uint32_t lba,
    uint32_t offset,
    const uint8_t* buffer,
    uint32_t bufsize
) {
    if (!ready || !buffer) {
        return false;
    }

    const uint64_t firstByte = static_cast<uint64_t>(lba) * SECTOR_SIZE + offset;
    const uint64_t endByte = firstByte + bufsize;
    const uint64_t imageBytes = static_cast<uint64_t>(totalSectors) * SECTOR_SIZE;
    if (endByte > imageBytes || endByte < firstByte) {
        return false;
    }

    if (!lock()) {
        return false;
    }

    const uint8_t* in = buffer;
    uint64_t absolute = firstByte;
    uint32_t remaining = bufsize;
    bool ok = true;

    while (remaining > 0 && ok) {
        const uint32_t sector = static_cast<uint32_t>(absolute / SECTOR_SIZE);
        const uint32_t sectorOffset = static_cast<uint32_t>(absolute % SECTOR_SIZE);

        if (sectorOffset == 0 && remaining >= SECTOR_SIZE) {
            const uint32_t fullSectors = remaining / SECTOR_SIZE;
            ok = writeImageSectors(sector, in, fullSectors);
            if (ok) {
                const uint32_t bytes = fullSectors * SECTOR_SIZE;
                in += bytes;
                absolute += bytes;
                remaining -= bytes;
            }
        } else {
            ok = readImageSectors(sector, scratch, 1);
            if (ok) {
                const uint32_t available = SECTOR_SIZE - sectorOffset;
                const uint32_t chunk = std::min(remaining, available);
                memcpy(scratch + sectorOffset, in, chunk);
                ok = writeImageSectors(sector, scratch, 1);
                if (ok) {
                    in += chunk;
                    absolute += chunk;
                    remaining -= chunk;
                }
            }
        }
    }

    unlock();
    return ok;
}

bool SDWholeCardStorage::sync() {
    if (!ready || !lock()) {
        return false;
    }
    const bool ok = disk_ioctl(pdrv, CTRL_SYNC, nullptr) == RES_OK;
    unlock();
    return ok;
}

void SDWholeCardStorage::service() {

}

bool SDWholeCardStorage::isReady() const {
    return ready;
}

uint32_t SDWholeCardStorage::sectorCount() const {
    return totalSectors;
}

uint16_t SDWholeCardStorage::sectorSize() const {
    return SECTOR_SIZE;
}

uint64_t SDWholeCardStorage::capacityBytes() const {
    return static_cast<uint64_t>(totalSectors) * SECTOR_SIZE;
}

uint32_t SDWholeCardStorage::partitionStartLba() const {
    return p1Start;
}

uint32_t SDWholeCardStorage::partitionSectorCount() const {
    return p1Sectors;
}

uint8_t SDWholeCardStorage::partitionType() const {
    return p1Type;
}

uint8_t SDWholeCardStorage::driveNumber() const {
    return pdrv;
}

const char* SDWholeCardStorage::lastError() const {
    return errorText;
}

uint64_t SDWholeCardStorage::physicalCapacityBytes() const {
    return static_cast<uint64_t>(physicalSectors) * SECTOR_SIZE;
}

uint32_t SDWholeCardStorage::imageClusterCount() const {
    return imageClustersUsed;
}
