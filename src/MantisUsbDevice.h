#ifndef MANTIS_USB_DEVICE_H
#define MANTIS_USB_DEVICE_H

#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDConsumerControl.h>
#include "DuckyScriptParser.h"

class MantisUsbDevice : public HIDDevice {
private:
    USBHIDKeyboard Keyboard;
    USBHIDMouse Mouse;
    USBHIDConsumerControl Consumer;
    HIDMode currentMode;
    volatile bool deviceConnected;
    volatile bool keyboardLedSeen;
    volatile bool capsLockOn;
    String keyboardLayoutName;
    bool externalLayoutActive;
    uint16_t externalLayout[128];
    String productName;
    String feedbackState;
    bool started;

    static MantisUsbDevice* instance;
    static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void keyboardEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    void sendRawUsage(uint8_t usage, bool shift = false, bool altGr = false);
    void sendRawUsageWithModifiers(uint8_t usage, uint8_t modifiers);
    void sendLatamChar(char c);
    void sendExternalChar(uint8_t c);
    void applyKeyboardLayout();
    uint8_t mouseButtonMask(const String& name) const;

public:
    MantisUsbDevice();
    bool begin();
    void setMode(HIDMode mode);
    void setProductName(const String& name);
    void setKeyboardLayout(const String& name, const uint16_t* table = nullptr);
    String getKeyboardLayoutName() const;

    void sendKey(uint8_t key, uint8_t modifiers = 0) override;
    void pressKey(uint8_t key, uint8_t modifiers = 0) override;
    void releaseAllKeys() override;
    void sendString(const String& text) override;
    void sendStringWithDelay(const String& text, uint32_t charDelayMs) override;
    void sendKeySequence(const String& keys) override;
    void sendAltCode(const String& digits) override;
    void mouseMove(int16_t x, int16_t y, int16_t wheel = 0) override;
    void mouseClick(const String& button) override;
    void mousePress(const String& button) override;
    void mouseRelease(const String& button) override;
    void consumerPress(uint16_t usage) override;
    void delay(uint32_t ms) override;
    bool isConnected() override;
    bool hasKeyboardLedReport() override;
    bool isCapsLockOn() override;
    bool ensureCapsLockOffFast(uint32_t timeoutMs = 250);
    void setFeedbackState(const String& state) override;

    String getFeedbackState() const;
    void setConnected(bool connected) { deviceConnected = connected; }
};

#endif
