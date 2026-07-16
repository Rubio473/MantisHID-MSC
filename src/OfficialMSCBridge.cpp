#include "OfficialMSCBridge.h"
#include "tusb_config.h"

#ifndef MANTIS_MSC_BUFFER_SIZE
#define MANTIS_MSC_BUFFER_SIZE 8192
#endif

static_assert(CFG_TUD_MSC_EP_BUFSIZE == MANTIS_MSC_BUFFER_SIZE,
              "MantisHID+MSC: TinyUSB MSC buffer override was not applied");

OfficialMSCBridge* OfficialMSCBridge::instance = nullptr;

OfficialMSCBridge::OfficialMSCBridge()
    : storage(nullptr),
      ejectPending(false),
      ejectedState(false),
      sharing(false),
      configured(false),
      configuredBlockCount(0),
      configuredBlockSize(0),
      shareStartedMs(0),
      firstReadDelayMs(UINT32_MAX),
      firstWriteDelayMs(UINT32_MAX),
      readCount(0),
      writeCount(0),
      readFailures(0),
      writeFailures(0),
      lastReadMs(0),
      maxReadMs(0),
      lastWriteMs(0),
      maxWriteMs(0),
      lastReadSize(0),
      lastWriteSize(0),
      lastIoActivityMs(0) {

    instance = this;
}

void OfficialMSCBridge::resetDiagnostics() {
    shareStartedMs = millis();
    firstReadDelayMs = UINT32_MAX;
    firstWriteDelayMs = UINT32_MAX;
    readCount = 0;
    writeCount = 0;
    readFailures = 0;
    writeFailures = 0;
    lastReadMs = 0;
    maxReadMs = 0;
    lastWriteMs = 0;
    maxWriteMs = 0;
    lastReadSize = 0;
    lastWriteSize = 0;
    lastIoActivityMs = millis();
}

bool OfficialMSCBridge::begin(SDWholeCardStorage* sdStorage) {
    storage = nullptr;
    ejectPending = false;
    ejectedState = false;
    sharing = false;
    configured = false;
    configuredBlockCount = 0;
    configuredBlockSize = 0;
    resetDiagnostics();

    const bool mediumReady = sdStorage != nullptr && sdStorage->isReady();
    const uint32_t blockCount = mediumReady ? sdStorage->sectorCount() : 1U;
    const uint16_t blockSize = mediumReady ? sdStorage->sectorSize() : 512U;

    if (blockCount == 0 || blockSize == 0) {
        return false;
    }

    msc.vendorID("MANTIS  ");
    msc.productID("MantisUSB       ");
    msc.productRevision("1.00");
    msc.onRead(readCallback);
    msc.onWrite(writeCallback);
    msc.onStartStop(startStopCallback);
    msc.isWritable(true);
    msc.mediaPresent(false);

    if (!msc.begin(blockCount, blockSize)) {
        return false;
    }

    configuredBlockCount = blockCount;
    configuredBlockSize = blockSize;
    configured = true;

    if (!mediumReady) {

        msc.mediaPresent(false);
        return true;
    }

    storage = sdStorage;
    resetDiagnostics();
    sharing = true;

    msc.mediaPresent(true);
    return true;
}

bool OfficialMSCBridge::shareDrive(bool notifyMediaChange) {
    (void)notifyMediaChange;

    if (!configured || !storage || !storage->isReady()) {
        return false;
    }

    ejectPending = false;
    ejectedState = false;
    resetDiagnostics();
    sharing = true;
    msc.mediaPresent(true);
    return true;
}

bool OfficialMSCBridge::detachDrive() {
    if (!storage) {
        return false;
    }

    sharing = false;
    msc.mediaPresent(false);
    return true;
}

bool OfficialMSCBridge::reclaimDrive() {
    if (!storage) {
        return false;
    }
    return storage->sync();
}

bool OfficialMSCBridge::consumeEjected() {
    if (!ejectPending) {
        return false;
    }
    ejectPending = false;
    return true;
}

bool OfficialMSCBridge::isSharing() const {
    return sharing;
}

uint32_t OfficialMSCBridge::lastIoAgeMs() const {
    return static_cast<uint32_t>(millis() - lastIoActivityMs);
}

MSCDiagnostics OfficialMSCBridge::diagnostics() const {
    MSCDiagnostics d{};
    d.sharedElapsedMs = sharing ? (millis() - shareStartedMs) : 0;
    d.firstReadDelayMs = firstReadDelayMs;
    d.firstWriteDelayMs = firstWriteDelayMs;
    d.readCount = readCount;
    d.writeCount = writeCount;
    d.readFailures = readFailures;
    d.writeFailures = writeFailures;
    d.lastReadMs = lastReadMs;
    d.maxReadMs = maxReadMs;
    d.lastWriteMs = lastWriteMs;
    d.maxWriteMs = maxWriteMs;
    d.lastReadSize = lastReadSize;
    d.lastWriteSize = lastWriteSize;

    d.testUnitReadyCount = 0;
    d.noMediaSenseCount = 0;
    d.unitAttentionCount = 0;
    d.scsiCount = 0;
    d.lastScsiOpcode = 0;
    return d;
}

int32_t OfficialMSCBridge::handleRead(
    uint32_t lba,
    uint32_t offset,
    void* buffer,
    uint32_t bufsize
) {
    if (!sharing || !storage || !storage->isReady() || !buffer) {
        return -1;
    }

    const uint32_t started = millis();
    lastIoActivityMs = started;
    if (firstReadDelayMs == UINT32_MAX) {
        firstReadDelayMs = started - shareStartedMs;
    }

    readCount++;
    lastReadSize = bufsize;

    const bool ok = storage->readBytes(lba, offset, buffer, bufsize);
    const uint32_t duration = millis() - started;
    lastIoActivityMs = millis();
    lastReadMs = duration;
    if (duration > maxReadMs) {
        maxReadMs = duration;
    }

    if (!ok) {
        readFailures++;
        return -1;
    }

    return static_cast<int32_t>(bufsize);
}

int32_t OfficialMSCBridge::handleWrite(
    uint32_t lba,
    uint32_t offset,
    uint8_t* buffer,
    uint32_t bufsize
) {
    if (!sharing || !storage || !storage->isReady() || !buffer) {
        return -1;
    }

    const uint32_t started = millis();
    lastIoActivityMs = started;
    if (firstWriteDelayMs == UINT32_MAX) {
        firstWriteDelayMs = started - shareStartedMs;
    }

    writeCount++;
    lastWriteSize = bufsize;

    const bool ok = storage->writeBytes(lba, offset, buffer, bufsize);
    const uint32_t duration = millis() - started;
    lastIoActivityMs = millis();
    lastWriteMs = duration;
    if (duration > maxWriteMs) {
        maxWriteMs = duration;
    }

    if (!ok) {
        writeFailures++;
        return -1;
    }

    return static_cast<int32_t>(bufsize);
}

bool OfficialMSCBridge::handleStartStop(
    uint8_t powerCondition,
    bool start,
    bool loadEject
) {
    (void)powerCondition;

    if (loadEject && !start) {

        if (!ejectedState) {
            ejectedState = true;
            ejectPending = true;
        }
    }

    return true;
}

int32_t OfficialMSCBridge::readCallback(
    uint32_t lba,
    uint32_t offset,
    void* buffer,
    uint32_t bufsize
) {
    return instance ? instance->handleRead(lba, offset, buffer, bufsize) : -1;
}

int32_t OfficialMSCBridge::writeCallback(
    uint32_t lba,
    uint32_t offset,
    uint8_t* buffer,
    uint32_t bufsize
) {
    return instance ? instance->handleWrite(lba, offset, buffer, bufsize) : -1;
}

bool OfficialMSCBridge::startStopCallback(
    uint8_t powerCondition,
    bool start,
    bool loadEject
) {
    return instance ? instance->handleStartStop(powerCondition, start, loadEject) : true;
}
