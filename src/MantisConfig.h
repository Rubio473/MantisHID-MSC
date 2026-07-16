#ifndef MANTIS_CONFIG_H
#define MANTIS_CONFIG_H

#include <Arduino.h>
#include "SDWholeCardStorage.h"
#include "WholeSdFat32Reader.h"

struct MantisConfig {
    uint32_t defaultDelayMs = 40;
    uint8_t screenBrightness = 100;
    bool showExecutionProgress = true;
    bool returnToBrowser = true;
    bool ledEnabled = true;
    uint8_t ledBrightness = 100;
    String usbProductName = "MantisUSB";
    String keyboardLayoutName = "US";
};

class MantisConfigManager {
public:
    MantisConfigManager();

    bool loadFromWholeSd(SDWholeCardStorage& storage);
    bool loadInternalOverride();
    bool saveInternal(const MantisConfig& newConfig);
    void apply(const MantisConfig& newConfig);

    const MantisConfig& get() const;
    bool loadedFromSd() const;
    bool loadedFromInternal() const;
    String sourceLabel() const;
    String layoutName() const;

private:
    static constexpr const char* SD_CONFIG_PATH = "/MantisSD/Settings/mantis.conf";
    static constexpr const char* NVS_NAMESPACE = "mantisv10";
    static constexpr const char* NVS_SETTINGS_KEY = "settings";

    MantisConfig config;
    bool loadedSd;
    bool loadedInternal;
    WholeSdFat32Reader reader;

    void resetDefaults();
    void parseIni(const String& content);
    String serialize(const MantisConfig& value) const;
    static MantisConfig sanitized(const MantisConfig& value);
    static bool parseBool(const String& value, bool& result);
    static uint32_t parseBoundedUnsigned(const String& value, uint32_t fallback, uint32_t minValue, uint32_t maxValue);
};

#endif
