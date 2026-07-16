#ifndef MEOW_STATUS_LIGHT_H
#define MEOW_STATUS_LIGHT_H

#include <Arduino.h>

class StatusLight {
public:
    StatusLight();
    void begin();
    void setColor565(uint16_t color);
    void off();
    void setEnabled(bool enabled);
    void setBrightness(uint8_t percent);
    void refresh();
    void writeCurrent();
    bool available() const;
    bool isEnabled() const;
    uint8_t getBrightness() const;
    int8_t getPin() const;

private:
    bool initialized;
    bool enabled;
    uint8_t brightnessPercent;
    uint16_t requestedColor;
};

#endif
