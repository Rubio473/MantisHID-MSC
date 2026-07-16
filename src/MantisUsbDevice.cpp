#include "MantisUsbDevice.h"

#if __has_include("tusb.h")
extern "C" {
#include "tusb.h"
}
#define MANTIS_HAS_TINYUSB_STATE 1
#else
#define MANTIS_HAS_TINYUSB_STATE 0
#endif

MantisUsbDevice* MantisUsbDevice::instance = nullptr;

namespace {
constexpr uint8_t HID_USAGE_A = 0x04;
constexpr uint8_t HID_USAGE_1 = 0x1E;
constexpr uint8_t HID_USAGE_0 = 0x27;
constexpr uint8_t HID_USAGE_SPACE = 0x2C;
constexpr uint8_t HID_USAGE_MINUS = 0x2D;
constexpr uint8_t HID_USAGE_RIGHT_BRACKET = 0x30;
constexpr uint8_t HID_USAGE_GRAVE = 0x35;
constexpr uint8_t HID_USAGE_COMMA = 0x36;
constexpr uint8_t HID_USAGE_DOT = 0x37;
constexpr uint8_t HID_USAGE_SLASH = 0x38;
constexpr uint8_t HID_USAGE_NON_US_BACKSLASH = 0x64;
constexpr uint8_t HID_USAGE_CAPS_LOCK = 0x39;
}

void MantisUsbDevice::usbEventCallback(void*, esp_event_base_t event_base, int32_t event_id, void*) {
    if (event_base != ARDUINO_USB_EVENTS || !instance) return;
    if (event_id == ARDUINO_USB_STARTED_EVENT || event_id == ARDUINO_USB_RESUME_EVENT) instance->setConnected(true);
    if (event_id == ARDUINO_USB_STOPPED_EVENT || event_id == ARDUINO_USB_SUSPEND_EVENT) instance->setConnected(false);
}

void MantisUsbDevice::keyboardEventCallback(void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (!instance || event_base != ARDUINO_USB_HID_KEYBOARD_EVENTS ||
        event_id != ARDUINO_USB_HID_KEYBOARD_LED_EVENT || !event_data) return;
    const auto* led = static_cast<const arduino_usb_hid_keyboard_event_data_t*>(event_data);
    instance->keyboardLedSeen = true;
    instance->capsLockOn = led->capslock;
}

MantisUsbDevice::MantisUsbDevice()
    : currentMode(HID_MODE_KEYBOARD),
      deviceConnected(false),
      keyboardLedSeen(false),
      capsLockOn(false),
      keyboardLayoutName("US"),
      externalLayoutActive(false),
      productName("MantisUSB"),
      feedbackState("READY"),
      started(false) {
    memset(externalLayout, 0, sizeof(externalLayout));
    instance = this;
}

void MantisUsbDevice::setProductName(const String&) {
    productName = "MantisUSB";
}

void MantisUsbDevice::setKeyboardLayout(const String& name, const uint16_t* table) {
    keyboardLayoutName = name.length() ? name : String("US");
    externalLayoutActive = table != nullptr;
    if (table) memcpy(externalLayout, table, sizeof(externalLayout));
    if (started) applyKeyboardLayout();
}

void MantisUsbDevice::applyKeyboardLayout() {
    if (keyboardLayoutName.equalsIgnoreCase("ES") || keyboardLayoutName.equalsIgnoreCase("LATAM")) {
        Keyboard.begin(KeyboardLayout_es_ES);
    } else {
        Keyboard.begin(KeyboardLayout_en_US);
    }
}

String MantisUsbDevice::getKeyboardLayoutName() const {
    return keyboardLayoutName;
}

bool MantisUsbDevice::begin() {
    deviceConnected = false;
    USB.manufacturerName("Mantis Project");
    USB.productName("MantisUSB");
    USB.serialNumber("MANTISUSB-0100");
    USB.onEvent(usbEventCallback);
    Keyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, keyboardEventCallback);
    applyKeyboardLayout();
    Mouse.begin();
    Consumer.begin();
    USB.begin();
    started = true;
    return true;
}

void MantisUsbDevice::setMode(HIDMode mode) {
    currentMode = mode;
}

void MantisUsbDevice::sendKey(uint8_t key, uint8_t modifiers) {
    if (!isConnected()) return;
    if (modifiers & DuckyScriptParser::MOD_CTRL_LEFT) Keyboard.press(KEY_LEFT_CTRL);
    if (modifiers & DuckyScriptParser::MOD_SHIFT_LEFT) Keyboard.press(KEY_LEFT_SHIFT);
    if (modifiers & DuckyScriptParser::MOD_ALT_LEFT) Keyboard.press(KEY_LEFT_ALT);
    if (modifiers & DuckyScriptParser::MOD_GUI_LEFT) Keyboard.press(KEY_LEFT_GUI);
    if (modifiers & DuckyScriptParser::MOD_CTRL_RIGHT) Keyboard.press(KEY_RIGHT_CTRL);
    if (modifiers & DuckyScriptParser::MOD_SHIFT_RIGHT) Keyboard.press(KEY_RIGHT_SHIFT);
    if (modifiers & DuckyScriptParser::MOD_ALT_RIGHT) Keyboard.press(KEY_RIGHT_ALT);
    if (modifiers & DuckyScriptParser::MOD_GUI_RIGHT) Keyboard.press(KEY_RIGHT_GUI);
    if (key != 0) Keyboard.press(key);
    ::delay(20);
    Keyboard.releaseAll();
    ::delay(12);
}

void MantisUsbDevice::pressKey(uint8_t key, uint8_t modifiers) {
    if (!isConnected()) return;
    if (modifiers & DuckyScriptParser::MOD_CTRL_LEFT) Keyboard.press(KEY_LEFT_CTRL);
    if (modifiers & DuckyScriptParser::MOD_SHIFT_LEFT) Keyboard.press(KEY_LEFT_SHIFT);
    if (modifiers & DuckyScriptParser::MOD_ALT_LEFT) Keyboard.press(KEY_LEFT_ALT);
    if (modifiers & DuckyScriptParser::MOD_GUI_LEFT) Keyboard.press(KEY_LEFT_GUI);
    if (modifiers & DuckyScriptParser::MOD_CTRL_RIGHT) Keyboard.press(KEY_RIGHT_CTRL);
    if (modifiers & DuckyScriptParser::MOD_SHIFT_RIGHT) Keyboard.press(KEY_RIGHT_SHIFT);
    if (modifiers & DuckyScriptParser::MOD_ALT_RIGHT) Keyboard.press(KEY_RIGHT_ALT);
    if (modifiers & DuckyScriptParser::MOD_GUI_RIGHT) Keyboard.press(KEY_RIGHT_GUI);
    if (key != 0) Keyboard.press(key);
    ::delay(12);
}

void MantisUsbDevice::releaseAllKeys() {
    Keyboard.releaseAll();
    Mouse.release(MOUSE_ALL);
    ::delay(12);
}

void MantisUsbDevice::sendRawUsageWithModifiers(uint8_t usage, uint8_t modifiers) {
    if (!isConnected() || usage == 0) return;
    if (modifiers & 0x01) Keyboard.press(KEY_LEFT_CTRL);
    if (modifiers & 0x02) Keyboard.press(KEY_LEFT_SHIFT);
    if (modifiers & 0x04) Keyboard.press(KEY_LEFT_ALT);
    if (modifiers & 0x08) Keyboard.press(KEY_LEFT_GUI);
    if (modifiers & 0x10) Keyboard.press(KEY_RIGHT_CTRL);
    if (modifiers & 0x20) Keyboard.press(KEY_RIGHT_SHIFT);
    if (modifiers & 0x40) Keyboard.press(KEY_RIGHT_ALT);
    if (modifiers & 0x80) Keyboard.press(KEY_RIGHT_GUI);
    Keyboard.pressRaw(usage);
    ::delay(10);
    Keyboard.releaseAll();
    ::delay(5);
}

void MantisUsbDevice::sendRawUsage(uint8_t usage, bool shift, bool altGr) {
    uint8_t modifiers = 0;
    if (shift) modifiers |= 0x02;
    if (altGr) modifiers |= 0x40;
    sendRawUsageWithModifiers(usage, modifiers);
}

void MantisUsbDevice::sendExternalChar(uint8_t c) {
    if (c >= 128) return;
    const uint16_t mapped = externalLayout[c];
    sendRawUsageWithModifiers(static_cast<uint8_t>(mapped & 0xFF), static_cast<uint8_t>((mapped >> 8) & 0xFF));
}

void MantisUsbDevice::sendLatamChar(char c) {
    if (c >= 'a' && c <= 'z') { sendRawUsage(HID_USAGE_A + c - 'a'); return; }
    if (c >= 'A' && c <= 'Z') { sendRawUsage(HID_USAGE_A + c - 'A', true); return; }
    if (c >= '1' && c <= '9') { sendRawUsage(HID_USAGE_1 + c - '1'); return; }
    if (c == '0') { sendRawUsage(HID_USAGE_0); return; }
    switch (c) {
        case ' ': sendRawUsage(HID_USAGE_SPACE); return;
        case '!': sendRawUsage(HID_USAGE_1, true); return;
        case '"': sendRawUsage(HID_USAGE_1 + 1, true); return;
        case '#': sendRawUsage(HID_USAGE_1 + 2, true); return;
        case '$': sendRawUsage(HID_USAGE_1 + 3, true); return;
        case '%': sendRawUsage(HID_USAGE_1 + 4, true); return;
        case '&': sendRawUsage(HID_USAGE_1 + 5, true); return;
        case '/': sendRawUsage(HID_USAGE_1 + 6, true); return;
        case '(': sendRawUsage(HID_USAGE_1 + 7, true); return;
        case ')': sendRawUsage(HID_USAGE_1 + 8, true); return;
        case '=': sendRawUsage(HID_USAGE_0, true); return;
        case '\'': sendRawUsage(HID_USAGE_MINUS); return;
        case '?': sendRawUsage(HID_USAGE_MINUS, true); return;
        case '+': sendRawUsage(HID_USAGE_RIGHT_BRACKET); return;
        case '*': sendRawUsage(HID_USAGE_RIGHT_BRACKET, true); return;
        case ',': sendRawUsage(HID_USAGE_COMMA); return;
        case ';': sendRawUsage(HID_USAGE_COMMA, true); return;
        case '.': sendRawUsage(HID_USAGE_DOT); return;
        case ':': sendRawUsage(HID_USAGE_DOT, true); return;
        case '-': sendRawUsage(HID_USAGE_SLASH); return;
        case '_': sendRawUsage(HID_USAGE_SLASH, true); return;
        case '@': sendRawUsage(0x14, false, true); return;
        case '\\': sendRawUsage(HID_USAGE_MINUS, false, true); return;
        case '|': sendRawUsage(HID_USAGE_GRAVE); return;
        case '<': sendRawUsage(HID_USAGE_NON_US_BACKSLASH); return;
        case '>': sendRawUsage(HID_USAGE_NON_US_BACKSLASH, true); return;
        default: Keyboard.write(static_cast<uint8_t>(c)); ::delay(8); return;
    }
}

void MantisUsbDevice::sendStringWithDelay(const String& text, uint32_t charDelayMs) {
    if (!isConnected()) return;
    for (size_t i = 0; i < text.length(); ++i) {
        const uint8_t byte = static_cast<uint8_t>(text[i]);
        if (externalLayoutActive && byte < 128) sendExternalChar(byte);
        else if (keyboardLayoutName.equalsIgnoreCase("LATAM") && byte < 128) sendLatamChar(static_cast<char>(byte));
        else { Keyboard.write(byte); ::delay(8); }
        if (charDelayMs > 0) ::delay(charDelayMs);
    }
    ::delay(15);
}

void MantisUsbDevice::sendString(const String& text) {
    sendStringWithDelay(text, 0);
}

void MantisUsbDevice::sendKeySequence(const String& keys) {
    sendString(keys);
}

void MantisUsbDevice::sendAltCode(const String& digits) {
    if (!isConnected() || digits.length() == 0) return;
    Keyboard.press(KEY_LEFT_ALT);
    for (size_t i = 0; i < digits.length(); ++i) {
        const char c = digits[i];
        uint8_t key = 0;
        if (c == '0') key = 0xEA;
        else if (c >= '1' && c <= '9') key = static_cast<uint8_t>(0xE1 + c - '1');
        else continue;
        Keyboard.press(key);
        ::delay(20);
        Keyboard.release(key);
    }
    Keyboard.releaseAll();
    ::delay(20);
}

uint8_t MantisUsbDevice::mouseButtonMask(const String& name) const {
    String value = name;
    value.toUpperCase();
    value.replace("_", "");
    if (value == "LEFT" || value == "LEFTCLICK") return MOUSE_LEFT;
    if (value == "RIGHT" || value == "RIGHTCLICK") return MOUSE_RIGHT;
    if (value == "MIDDLE" || value == "MIDDLECLICK" || value == "WHEELCLICK") return MOUSE_MIDDLE;
    if (value == "BACK" || value == "BACKWARD") return MOUSE_BACKWARD;
    if (value == "FORWARD") return MOUSE_FORWARD;
    return 0;
}

void MantisUsbDevice::mouseMove(int16_t x, int16_t y, int16_t wheel) {
    if (!isConnected()) return;
    while (x != 0 || y != 0 || wheel != 0) {
        const int8_t sx = static_cast<int8_t>(x < -127 ? -127 : (x > 127 ? 127 : x));
        const int8_t sy = static_cast<int8_t>(y < -127 ? -127 : (y > 127 ? 127 : y));
        const int8_t sw = static_cast<int8_t>(wheel < -127 ? -127 : (wheel > 127 ? 127 : wheel));
        Mouse.move(sx, sy, sw, 0);
        x -= sx;
        y -= sy;
        wheel -= sw;
        ::delay(8);
    }
}

void MantisUsbDevice::mouseClick(const String& button) {
    const uint8_t mask = mouseButtonMask(button);
    if (isConnected() && mask) Mouse.click(mask);
}

void MantisUsbDevice::mousePress(const String& button) {
    const uint8_t mask = mouseButtonMask(button);
    if (isConnected() && mask) Mouse.press(mask);
}

void MantisUsbDevice::mouseRelease(const String& button) {
    const uint8_t mask = mouseButtonMask(button);
    if (isConnected() && mask) Mouse.release(mask);
}

void MantisUsbDevice::consumerPress(uint16_t usage) {
    if (!isConnected() || usage == 0) return;
    Consumer.press(usage);
    ::delay(25);
    Consumer.release();
    ::delay(15);
}

void MantisUsbDevice::delay(uint32_t ms) {
    ::delay(ms);
}

bool MantisUsbDevice::isConnected() {
    if (!started) return false;
#if MANTIS_HAS_TINYUSB_STATE
    return tud_mounted() && !tud_suspended();
#else
    return deviceConnected;
#endif
}

bool MantisUsbDevice::hasKeyboardLedReport() { return keyboardLedSeen; }
bool MantisUsbDevice::isCapsLockOn() { return capsLockOn; }

bool MantisUsbDevice::ensureCapsLockOffFast(uint32_t timeoutMs) {
    if (!isConnected()) return false;
    auto pressCapsLock = [this]() {
        Keyboard.pressRaw(HID_USAGE_CAPS_LOCK);
        ::delay(12);
        Keyboard.releaseAll();
        ::delay(12);
    };
    if (keyboardLedSeen && !capsLockOn) return true;
    pressCapsLock();
    const uint32_t firstWaitStarted = millis();
    while (isConnected() && millis() - firstWaitStarted < timeoutMs) {
        if (keyboardLedSeen) {
            if (!capsLockOn) return true;
            pressCapsLock();
            const uint32_t confirmStarted = millis();
            while (isConnected() && millis() - confirmStarted < timeoutMs) {
                if (keyboardLedSeen && !capsLockOn) return true;
                ::delay(2);
            }
            return keyboardLedSeen && !capsLockOn;
        }
        ::delay(2);
    }
    return false;
}

void MantisUsbDevice::setFeedbackState(const String& state) {
    String normalized = state;
    normalized.toUpperCase();
    if (!(feedbackState == normalized)) feedbackState = normalized;
}

String MantisUsbDevice::getFeedbackState() const { return feedbackState; }
