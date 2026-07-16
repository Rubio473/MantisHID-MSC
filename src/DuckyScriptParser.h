#ifndef DUCKYSCRIPT_PARSER_H
#define DUCKYSCRIPT_PARSER_H

#include <Arduino.h>
#include <vector>
#include <map>

class HIDDevice {
public:
    virtual ~HIDDevice() = default;
    virtual void sendKey(uint8_t key, uint8_t modifiers = 0) = 0;
    virtual void pressKey(uint8_t key, uint8_t modifiers = 0) = 0;
    virtual void releaseAllKeys() = 0;
    virtual void sendString(const String& text) = 0;
    virtual void sendStringWithDelay(const String& text, uint32_t charDelayMs) = 0;
    virtual void sendKeySequence(const String& keys) = 0;
    virtual void sendAltCode(const String& digits) = 0;
    virtual void mouseMove(int16_t x, int16_t y, int16_t wheel = 0) = 0;
    virtual void mouseClick(const String& button) = 0;
    virtual void mousePress(const String& button) = 0;
    virtual void mouseRelease(const String& button) = 0;
    virtual void consumerPress(uint16_t usage) = 0;
    virtual void delay(uint32_t ms) = 0;
    virtual bool isConnected() = 0;
    virtual bool hasKeyboardLedReport() = 0;
    virtual bool isCapsLockOn() = 0;
    virtual void setFeedbackState(const String& state) = 0;
};

enum HIDMode {
    HID_MODE_KEYBOARD
};

class DuckyScriptParser {
public:
    DuckyScriptParser();

    void setHIDDevice(HIDDevice* device);
    void setInitialDefaultDelay(uint32_t delayMs);
    void setButtonCallback(bool (*callback)());
    void execute(const String& script);
    void process();
    String getCurrentLine();
    String getLastProcessedLine() const;
    size_t getProcessedLineCount() const;
    size_t getTotalLineCount() const;
    void executeLine(const String& line);
    bool isExecutionComplete() const { return executionComplete; }
    void stopExecution();

    bool isWaiting() const;
    uint32_t getRemainingWaitMs() const;
    String getWaitReason() const;
    bool hasError() const { return executionError.length() > 0; }
    String getError() const { return executionError; }
    uint32_t getErrorLine() const { return executionErrorLine; }

    static const uint8_t DUCKY_ENTER = 0xB0;
    static const uint8_t DUCKY_ESC = 0xB1;
    static const uint8_t DUCKY_BACKSPACE = 0xB2;
    static const uint8_t DUCKY_TAB = 0xB3;
    static const uint8_t DUCKY_SPACE = ' ';
    static const uint8_t DUCKY_INSERT = 0xD1;
    static const uint8_t DUCKY_HOME = 0xD2;
    static const uint8_t DUCKY_PAGE_UP = 0xD3;
    static const uint8_t DUCKY_DELETE = 0xD4;
    static const uint8_t DUCKY_END = 0xD5;
    static const uint8_t DUCKY_PAGE_DOWN = 0xD6;
    static const uint8_t DUCKY_RIGHT = 0xD7;
    static const uint8_t DUCKY_LEFT = 0xD8;
    static const uint8_t DUCKY_DOWN = 0xD9;
    static const uint8_t DUCKY_UP = 0xDA;
    static const uint8_t DUCKY_CAPS_LOCK = 0xC1;
    static const uint8_t DUCKY_F1 = 0xC2;
    static const uint8_t DUCKY_F12 = 0xCD;
    static const uint8_t DUCKY_F13 = 0xF0;
    static const uint8_t DUCKY_F24 = 0xFB;
    static const uint8_t DUCKY_PRINT_SCREEN = 0xCE;
    static const uint8_t DUCKY_SCROLL_LOCK = 0xCF;
    static const uint8_t DUCKY_PAUSE = 0xD0;
    static const uint8_t DUCKY_NUM_LOCK = 0xDB;
    static const uint8_t DUCKY_MENU = 0xED;

    static const uint8_t MOD_CTRL_LEFT   = 0x01;
    static const uint8_t MOD_SHIFT_LEFT  = 0x02;
    static const uint8_t MOD_ALT_LEFT    = 0x04;
    static const uint8_t MOD_GUI_LEFT    = 0x08;
    static const uint8_t MOD_CTRL_RIGHT  = 0x10;
    static const uint8_t MOD_SHIFT_RIGHT = 0x20;
    static const uint8_t MOD_ALT_RIGHT   = 0x40;
    static const uint8_t MOD_GUI_RIGHT   = 0x80;

private:
    enum class WaitCondition : uint8_t {
        NONE,
        TIME,
        USB,
        CAPSLOCK,
        BUTTON
    };

    HIDDevice* hidDevice;
    bool executionComplete;
    uint32_t configuredDefaultDelay;
    unsigned long commandDelay;
    uint32_t defaultStringDelay;
    uint32_t nextStringDelay;
    bool (*buttonPressedCallback)();

    std::map<String, uint8_t> specialKeys;

    std::vector<String> lines;
    int currentLine;
    bool inCommentBlock;
    String lastProcessedLine;
    String lastRepeatableLine;
    String repeatLine;
    uint32_t repeatRemaining;
    size_t processedSteps;
    size_t totalSteps;

    WaitCondition waitCondition;
    uint32_t waitStartedMs;
    uint32_t waitDurationMs;
    uint32_t waitTimeoutMs;

    String executionError;
    uint32_t executionErrorLine;

    void executeLineInternal(const String& line, bool sourceLine);
    void handleREM_BLOCK(const String& line);
    uint32_t handleDELAY(const String& line);
    void handleSTRING(const String& line);
    void handleSTRINGLN(const String& line);
    void handleSTRINGDELAY(const String& line);
    void handleDEFAULTSTRINGDELAY(const String& line);
    void handleALTCHAR(const String& line);
    void handleALTSTRING(const String& line);
    void handleSYSRQ(const String& line);
    void handleMEDIA(const String& line);
    void handleMouseCommand(const String& command, const String& line);
    bool handleSafeKey(const String& token);
    bool handleKeyCombination(const String& command, const String& parameters);
    bool parseKeyToken(const String& token, uint8_t& key) const;
    uint8_t modifierForToken(const String& token) const;
    void handleHOLD(const String& parameters);
    void handleRELEASE();
    void handleDEFAULTDELAY(const String& line);
    void handleREPEAT(const String& parameters);
    void handleWAIT_FOR_USB(const String& parameters);
    void handleWAIT_FOR_CAPSLOCK(const String& parameters);
    void handleLED(const String& parameters);
    void scheduleWait(uint32_t ms);
    void scheduleConditionWait(WaitCondition condition, uint32_t timeoutMs);
    bool updateWaitCondition();
    void fail(const String& message);
    bool isRepeatableCommand(const String& command) const;
    bool nextSourceCommandIsWaitForUsb() const;
    size_t estimateTotalSteps() const;

    String trim(const String& str) const;
    bool isWhitespace(char c) const;
    static uint32_t parsePositiveOrDefault(const String& value, uint32_t fallback);
};

#endif
