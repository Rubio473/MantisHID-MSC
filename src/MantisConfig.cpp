#include "MantisConfig.h"

#include <Preferences.h>

MantisConfigManager::MantisConfigManager() : loadedSd(false), loadedInternal(false) {
    resetDefaults();
}

void MantisConfigManager::resetDefaults() {
    config.defaultDelayMs = 40;
    config.screenBrightness = 100;
    config.showExecutionProgress = true;
    config.returnToBrowser = true;
    config.ledEnabled = true;
    config.ledBrightness = 100;
    config.usbProductName = "MantisUSB";
    config.keyboardLayoutName = "US";
}

bool MantisConfigManager::parseBool(const String& value, bool& result) {
    String normalized(value);
    normalized.trim();
    normalized.toLowerCase();
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        result = true;
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        result = false;
        return true;
    }
    return false;
}

uint32_t MantisConfigManager::parseBoundedUnsigned(
    const String& value,
    uint32_t fallback,
    uint32_t minValue,
    uint32_t maxValue
) {
    if (value.length() == 0) {
        return fallback;
    }

    uint64_t parsed = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') {
            return fallback;
        }
        parsed = parsed * 10ULL + static_cast<uint64_t>(c - '0');
        if (parsed > maxValue) {
            return maxValue;
        }
    }

    if (parsed < minValue) {
        return minValue;
    }
    return static_cast<uint32_t>(parsed);
}

MantisConfig MantisConfigManager::sanitized(const MantisConfig& value) {
    MantisConfig result = value;
    if (result.defaultDelayMs > 2000) result.defaultDelayMs = 2000;
    if (result.screenBrightness < 1) result.screenBrightness = 1;
    if (result.screenBrightness > 100) result.screenBrightness = 100;
    if (result.ledBrightness < 10) result.ledBrightness = 10;
    if (result.ledBrightness > 100) result.ledBrightness = 100;

    result.usbProductName = "MantisUSB";
    result.keyboardLayoutName.trim();
    if (result.keyboardLayoutName.length() == 0 || result.keyboardLayoutName.length() > 31) result.keyboardLayoutName = "US";
    return result;
}

void MantisConfigManager::parseIni(const String& content) {
    int start = 0;
    while (start <= static_cast<int>(content.length())) {
        int end = content.indexOf('\n', start);
        if (end < 0) {
            end = content.length();
        }

        String line = content.substring(start, end);
        line.trim();
        if (line.length() > 0 && !line.startsWith("#") && !line.startsWith(";")) {
            const int equals = line.indexOf('=');
            if (equals > 0) {
                String key = line.substring(0, equals);
                String value = line.substring(equals + 1);
                key.trim();
                value.trim();
                key.toLowerCase();

                if (key == "default_delay") {
                    config.defaultDelayMs = parseBoundedUnsigned(value, config.defaultDelayMs, 0, 2000);
                } else if (key == "screen_brightness") {
                    config.screenBrightness = static_cast<uint8_t>(
                        parseBoundedUnsigned(value, config.screenBrightness, 1, 100)
                    );
                } else if (key == "show_execution_progress") {
                    bool parsed = config.showExecutionProgress;
                    if (parseBool(value, parsed)) config.showExecutionProgress = parsed;
                } else if (key == "return_to_browser") {
                    bool parsed = config.returnToBrowser;
                    if (parseBool(value, parsed)) config.returnToBrowser = parsed;
                } else if (key == "led_enabled") {
                    bool parsed = config.ledEnabled;
                    if (parseBool(value, parsed)) config.ledEnabled = parsed;
                } else if (key == "led_brightness") {
                    config.ledBrightness = static_cast<uint8_t>(
                        parseBoundedUnsigned(value, config.ledBrightness, 10, 100)
                    );
                } else if (key == "usb_product_name") {

                    config.usbProductName = "MantisUSB";
                } else if (key == "keyboard_layout") {
                    value.trim();
                    if (value.length() > 0 && value.length() <= 31) config.keyboardLayoutName = value;
                }
            }
        }

        if (end >= static_cast<int>(content.length())) break;
        start = end + 1;
    }

    config = sanitized(config);
}

String MantisConfigManager::serialize(const MantisConfig& value) const {
    const MantisConfig clean = sanitized(value);
    String output;
    output.reserve(280);
    output += "default_delay=" + String(clean.defaultDelayMs) + "\n";
    output += "screen_brightness=" + String(clean.screenBrightness) + "\n";
    output += String("show_execution_progress=") + (clean.showExecutionProgress ? "true" : "false") + "\n";
    output += String("return_to_browser=") + (clean.returnToBrowser ? "true" : "false") + "\n";
    output += String("led_enabled=") + (clean.ledEnabled ? "true" : "false") + "\n";
    output += "led_brightness=" + String(clean.ledBrightness) + "\n";
    output += "keyboard_layout=" + clean.keyboardLayoutName + "\n";
    return output;
}

bool MantisConfigManager::loadFromWholeSd(SDWholeCardStorage& storage) {
    resetDefaults();
    loadedSd = false;
    loadedInternal = false;

    if (!reader.begin(storage)) {
        Serial.println("CONFIG: raw FAT32 reader unavailable; defaults active");
        return false;
    }

    Serial.printf("CONFIG DIR: %s\n", reader.directoryExists("/MantisSD/Settings") ? "READY" : "MISSING");

    String content;
    if (!reader.readFile(SD_CONFIG_PATH, content, 4096)) {
        Serial.println("CONFIG: /MantisSD/Settings/mantis.conf not found; defaults active");
        return false;
    }

    parseIni(content);
    loadedSd = true;
    Serial.println("CONFIG: loaded /MantisSD/Settings/mantis.conf");
    return true;
}

bool MantisConfigManager::loadInternalOverride() {
    loadedInternal = false;

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        Serial.println("CONFIG: NVS preferences unavailable");
        return false;
    }

    const String content = preferences.getString(NVS_SETTINGS_KEY, "");
    preferences.end();

    if (content.length() == 0) {
        Serial.println("CONFIG: no NVS saved override");
        return false;
    }
    if (content.length() > 4096) {
        Serial.println("CONFIG: NVS saved override too large");
        return false;
    }

    parseIni(content);
    loadedInternal = true;
    Serial.println("CONFIG: applied NVS saved override");
    return true;
}

bool MantisConfigManager::saveInternal(const MantisConfig& newConfig) {
    const MantisConfig clean = sanitized(newConfig);
    const String content = serialize(clean);

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        Serial.println("CONFIG: cannot open NVS preferences");
        return false;
    }

    const size_t written = preferences.putString(NVS_SETTINGS_KEY, content);
    preferences.end();
    if (written != content.length()) {
        Serial.println("CONFIG: NVS save incomplete");
        return false;
    }

    config = clean;
    loadedInternal = true;
    Serial.println("CONFIG: saved NVS override");
    return true;
}

void MantisConfigManager::apply(const MantisConfig& newConfig) {
    config = sanitized(newConfig);
}

const MantisConfig& MantisConfigManager::get() const {
    return config;
}

bool MantisConfigManager::loadedFromSd() const {
    return loadedSd;
}

bool MantisConfigManager::loadedFromInternal() const {
    return loadedInternal;
}

String MantisConfigManager::sourceLabel() const {
    if (loadedInternal) return "NVS saved settings";
    if (loadedSd) return "MantisSD/Settings/mantis.conf";
    return "built-in defaults";
}

String MantisConfigManager::layoutName() const {
    return config.keyboardLayoutName;
}
