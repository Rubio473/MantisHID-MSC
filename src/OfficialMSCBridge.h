#ifndef MANTIS_OFFICIAL_MSC_BRIDGE_H
#define MANTIS_OFFICIAL_MSC_BRIDGE_H

#include <Arduino.h>
#include <USBMSC.h>
#include "SDWholeCardStorage.h"

struct MSCDiagnostics {
    uint32_t sharedElapsedMs;
    uint32_t firstReadDelayMs;
    uint32_t firstWriteDelayMs;
    uint32_t readCount;
    uint32_t writeCount;
    uint32_t readFailures;
    uint32_t writeFailures;
    uint32_t lastReadMs;
    uint32_t maxReadMs;
    uint32_t lastWriteMs;
    uint32_t maxWriteMs;
    uint32_t lastReadSize;
    uint32_t lastWriteSize;
    uint32_t testUnitReadyCount;
    uint32_t noMediaSenseCount;
    uint32_t unitAttentionCount;
    uint32_t scsiCount;
    uint8_t lastScsiOpcode;
};

class OfficialMSCBridge {
public:
    OfficialMSCBridge();

    bool begin(SDWholeCardStorage* sdStorage);
    bool shareDrive(bool notifyMediaChange = false);
    bool detachDrive();
    bool reclaimDrive();

    bool consumeEjected();
    bool isSharing() const;
    uint32_t lastIoAgeMs() const;
    MSCDiagnostics diagnostics() const;

private:
    USBMSC msc;
    SDWholeCardStorage* storage;
    volatile bool ejectPending;
    volatile bool ejectedState;
    volatile bool sharing;
    bool configured;
    uint32_t configuredBlockCount;
    uint16_t configuredBlockSize;

    volatile uint32_t shareStartedMs;
    volatile uint32_t firstReadDelayMs;
    volatile uint32_t firstWriteDelayMs;
    volatile uint32_t readCount;
    volatile uint32_t writeCount;
    volatile uint32_t readFailures;
    volatile uint32_t writeFailures;
    volatile uint32_t lastReadMs;
    volatile uint32_t maxReadMs;
    volatile uint32_t lastWriteMs;
    volatile uint32_t maxWriteMs;
    volatile uint32_t lastReadSize;
    volatile uint32_t lastWriteSize;
    volatile uint32_t lastIoActivityMs;

    void resetDiagnostics();
    int32_t handleRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    int32_t handleWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
    bool handleStartStop(uint8_t powerCondition, bool start, bool loadEject);

    static int32_t readCallback(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    static int32_t writeCallback(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
    static bool startStopCallback(uint8_t powerCondition, bool start, bool loadEject);

    static OfficialMSCBridge* instance;
};

#endif
