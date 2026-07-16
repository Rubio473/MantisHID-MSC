#include "StatusLight.h"
#include <M5Cardputer.h>

StatusLight::StatusLight()
    : initialized(false),
      enabled(true),
      brightnessPercent(30),
      requestedColor(0) {
}

void StatusLight::begin() {
    initialized = M5.Led.begin();
    requestedColor = 0;

    if (initialized) {

        M5.Led.setBrightness(255);
        writeCurrent();
    }

    Serial.printf(
        "STATUS LED: available=%s board=%d pin=%d brightness=%u%%\n",
        initialized ? "YES" : "NO",
        static_cast<int>(M5.getBoard()),
        static_cast<int>(M5.getPin(m5::pin_name_t::rgb_led)),
        static_cast<unsigned>(brightnessPercent)
    );
}

void StatusLight::setColor565(uint16_t color) {
    requestedColor = color;
}

void StatusLight::off() {
    requestedColor = 0;
}

void StatusLight::setEnabled(bool value) {
    enabled = value;
}

void StatusLight::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    if (percent < 1) percent = 1;
    brightnessPercent = percent;
}

void StatusLight::refresh() {
    writeCurrent();
}

void StatusLight::writeCurrent() {
    if (!initialized) return;

    const uint16_t physicalColor = enabled ? requestedColor : 0;
    const uint8_t red = static_cast<uint8_t>(((physicalColor >> 11) & 0x1F) * 255 / 31);
    const uint8_t green = static_cast<uint8_t>(((physicalColor >> 5) & 0x3F) * 255 / 63);
    const uint8_t blue = static_cast<uint8_t>((physicalColor & 0x1F) * 255 / 31);

    const uint8_t scaledRed = static_cast<uint8_t>(
        static_cast<uint16_t>(red) * brightnessPercent / 100U
    );
    const uint8_t scaledGreen = static_cast<uint8_t>(
        static_cast<uint16_t>(green) * brightnessPercent / 100U
    );
    const uint8_t scaledBlue = static_cast<uint8_t>(
        static_cast<uint16_t>(blue) * brightnessPercent / 100U
    );

    M5.Led.setAllColor(RGBColor{scaledRed, scaledGreen, scaledBlue});
}

bool StatusLight::available() const {
    return initialized;
}

bool StatusLight::isEnabled() const {
    return enabled;
}

uint8_t StatusLight::getBrightness() const {
    return brightnessPercent;
}

int8_t StatusLight::getPin() const {
    return M5.getPin(m5::pin_name_t::rgb_led);
}
