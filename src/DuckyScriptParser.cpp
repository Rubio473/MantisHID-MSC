#include "DuckyScriptParser.h"

namespace {
constexpr uint32_t DEFAULT_COMMAND_DELAY_MS = 40;
constexpr uint32_t CONDITION_WAIT_TIMEOUT_MS = 30000;
constexpr uint32_t MAX_REPEAT_COUNT = 1000;

uint16_t mediaUsageFor(const String& source) {
    String value = source;
    value.trim();
    value.toUpperCase();
    value.replace("-", "_");
    if (value == "POWER") return 0x0030;
    if (value == "REBOOT" || value == "RESET") return 0x0031;
    if (value == "SLEEP") return 0x0032;
    if (value == "BRIGHT_UP" || value == "BRIGHTNESS_UP") return 0x006F;
    if (value == "BRIGHT_DOWN" || value == "BRIGHTNESS_DOWN") return 0x0070;
    if (value == "NEXT_TRACK" || value == "NEXT") return 0x00B5;
    if (value == "PREV_TRACK" || value == "PREVIOUS_TRACK" || value == "PREVIOUS") return 0x00B6;
    if (value == "STOP") return 0x00B7;
    if (value == "EJECT") return 0x00B8;
    if (value == "PLAY" || value == "PAUSE" || value == "PLAY_PAUSE") return 0x00CD;
    if (value == "MUTE") return 0x00E2;
    if (value == "VOLUME_UP" || value == "VOL_UP") return 0x00E9;
    if (value == "VOLUME_DOWN" || value == "VOL_DOWN") return 0x00EA;
    if (value == "HOME") return 0x0223;
    if (value == "BACK") return 0x0224;
    if (value == "FORWARD") return 0x0225;
    if (value == "REFRESH") return 0x0227;
    if (value == "SEARCH") return 0x0221;
    if (value == "BOOKMARKS") return 0x022A;
    if (value == "EMAIL") return 0x018A;
    if (value == "CALCULATOR") return 0x0192;
    if (value == "BROWSER" || value == "LOCAL_BROWSER") return 0x0194;
    return 0;
}

bool parseSignedPair(const String& source, int16_t& first, int16_t& second) {
    String value = source;
    value.trim();
    const int split = value.indexOf(' ');
    if (split < 0) return false;
    String left = value.substring(0, split);
    String right = value.substring(split + 1);
    left.trim();
    right.trim();
    if (left.length() == 0 || right.length() == 0) return false;
    first = static_cast<int16_t>(left.toInt());
    second = static_cast<int16_t>(right.toInt());
    return true;
}
}

DuckyScriptParser::DuckyScriptParser()
    : hidDevice(nullptr),
      executionComplete(true),
      configuredDefaultDelay(DEFAULT_COMMAND_DELAY_MS),
      commandDelay(DEFAULT_COMMAND_DELAY_MS),
      defaultStringDelay(0),
      nextStringDelay(0),
      buttonPressedCallback(nullptr),
      currentLine(0),
      inCommentBlock(false),
      lastProcessedLine(""),
      lastRepeatableLine(""),
      repeatLine(""),
      repeatRemaining(0),
      processedSteps(0),
      totalSteps(0),
      waitCondition(WaitCondition::NONE),
      waitStartedMs(0),
      waitDurationMs(0),
      waitTimeoutMs(0),
      executionError(""),
      executionErrorLine(0) {
    specialKeys["ENTER"] = DUCKY_ENTER;
    specialKeys["TAB"] = DUCKY_TAB;
    specialKeys["SPACE"] = DUCKY_SPACE;
    specialKeys["BACKSPACE"] = DUCKY_BACKSPACE;
    specialKeys["DELETE"] = DUCKY_DELETE;
    specialKeys["ESC"] = DUCKY_ESC;
    specialKeys["ESCAPE"] = DUCKY_ESC;
    specialKeys["UP"] = DUCKY_UP;
    specialKeys["UPARROW"] = DUCKY_UP;
    specialKeys["DOWN"] = DUCKY_DOWN;
    specialKeys["DOWNARROW"] = DUCKY_DOWN;
    specialKeys["LEFT"] = DUCKY_LEFT;
    specialKeys["LEFTARROW"] = DUCKY_LEFT;
    specialKeys["RIGHT"] = DUCKY_RIGHT;
    specialKeys["RIGHTARROW"] = DUCKY_RIGHT;
    specialKeys["HOME"] = DUCKY_HOME;
    specialKeys["END"] = DUCKY_END;
    specialKeys["PAGEUP"] = DUCKY_PAGE_UP;
    specialKeys["PAGE_UP"] = DUCKY_PAGE_UP;
    specialKeys["PAGEDOWN"] = DUCKY_PAGE_DOWN;
    specialKeys["PAGE_DOWN"] = DUCKY_PAGE_DOWN;
    specialKeys["PGUP"] = DUCKY_PAGE_UP;
    specialKeys["PGDN"] = DUCKY_PAGE_DOWN;
    specialKeys["RETURN"] = DUCKY_ENTER;
    specialKeys["BKSP"] = DUCKY_BACKSPACE;
    specialKeys["DEL"] = DUCKY_DELETE;
    specialKeys["INSERT"] = DUCKY_INSERT;
    specialKeys["INS"] = DUCKY_INSERT;
    specialKeys["CAPSLOCK"] = DUCKY_CAPS_LOCK;
    specialKeys["CAPS_LOCK"] = DUCKY_CAPS_LOCK;
    specialKeys["NUMLOCK"] = DUCKY_NUM_LOCK;
    specialKeys["NUM_LOCK"] = DUCKY_NUM_LOCK;
    specialKeys["SCROLLLOCK"] = DUCKY_SCROLL_LOCK;
    specialKeys["SCROLL_LOCK"] = DUCKY_SCROLL_LOCK;
    specialKeys["PRINTSCREEN"] = DUCKY_PRINT_SCREEN;
    specialKeys["PRINT_SCREEN"] = DUCKY_PRINT_SCREEN;
    specialKeys["PRTSC"] = DUCKY_PRINT_SCREEN;
    specialKeys["SYSRQ"] = DUCKY_PRINT_SCREEN;
    specialKeys["PAUSE"] = DUCKY_PAUSE;
    specialKeys["BREAK"] = DUCKY_PAUSE;
    specialKeys["MENU"] = DUCKY_MENU;
    specialKeys["APP"] = DUCKY_MENU;
    specialKeys["APPLICATION"] = DUCKY_MENU;
    for (uint8_t i = 1; i <= 12; ++i) {
        specialKeys[String("F") + String(i)] = static_cast<uint8_t>(DUCKY_F1 + (i - 1));
    }
    for (uint8_t i = 13; i <= 24; ++i) {
        specialKeys[String("F") + String(i)] = static_cast<uint8_t>(DUCKY_F13 + (i - 13));
    }
    specialKeys["KP_SLASH"] = 0xDC;
    specialKeys["KP_ASTERISK"] = 0xDD;
    specialKeys["KP_MINUS"] = 0xDE;
    specialKeys["KP_PLUS"] = 0xDF;
    specialKeys["KP_ENTER"] = 0xE0;
    for (uint8_t i = 1; i <= 9; ++i) {
        specialKeys[String("KP_") + String(i)] = static_cast<uint8_t>(0xE1 + (i - 1));
        specialKeys[String("NUMPAD") + String(i)] = static_cast<uint8_t>(0xE1 + (i - 1));
    }
    specialKeys["KP_0"] = 0xEA;
    specialKeys["NUMPAD0"] = 0xEA;
    specialKeys["KP_DOT"] = 0xEB;
}

void DuckyScriptParser::setHIDDevice(HIDDevice* device) {
    hidDevice = device;
}

void DuckyScriptParser::setInitialDefaultDelay(uint32_t delayMs) {
    configuredDefaultDelay = delayMs > 2000 ? 2000 : delayMs;
}

void DuckyScriptParser::setButtonCallback(bool (*callback)()) {
    buttonPressedCallback = callback;
}

uint32_t DuckyScriptParser::parsePositiveOrDefault(const String& value, uint32_t fallback) {
    if (value.length() == 0) {
        return fallback;
    }
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] < '0' || value[i] > '9') {
            return fallback;
        }
    }
    const uint32_t parsed = static_cast<uint32_t>(value.toInt());
    return parsed == 0 ? fallback : parsed;
}

void DuckyScriptParser::execute(const String& script) {
    executionComplete = true;
    executionError = "";
    executionErrorLine = 0;
    waitCondition = WaitCondition::NONE;
    waitStartedMs = 0;
    waitDurationMs = 0;
    waitTimeoutMs = 0;
    currentLine = 0;
    inCommentBlock = false;
    lastProcessedLine = "";
    lastRepeatableLine = "";
    repeatLine = "";
    repeatRemaining = 0;
    processedSteps = 0;
    totalSteps = 0;
    commandDelay = configuredDefaultDelay;
    defaultStringDelay = 0;
    nextStringDelay = 0;
    lines.clear();

    if (!hidDevice) {
        executionError = "HID device unavailable";
        return;
    }

    int start = 0;
    int end = script.indexOf('\n');
    while (end != -1) {
        lines.push_back(script.substring(start, end));
        start = end + 1;
        end = script.indexOf('\n', start);
    }
    if (start < static_cast<int>(script.length())) {
        lines.push_back(script.substring(start));
    }

    totalSteps = estimateTotalSteps();
    executionComplete = lines.empty();
    Serial.println("Starting cooperative safe script execution");
    Serial.println("Source lines: " + String(lines.size()));
    Serial.println("Estimated steps: " + String(totalSteps));
}

bool DuckyScriptParser::updateWaitCondition() {
    if (waitCondition == WaitCondition::NONE) {
        return true;
    }

    const uint32_t elapsed = static_cast<uint32_t>(millis() - waitStartedMs);
    switch (waitCondition) {
        case WaitCondition::TIME:
            if (elapsed < waitDurationMs) {
                return false;
            }
            break;

        case WaitCondition::USB:
            if (hidDevice && hidDevice->isConnected()) {
                break;
            }
            if (elapsed >= waitTimeoutMs) {
                fail("WAIT_FOR_USB timeout");
                return false;
            }
            return false;

        case WaitCondition::CAPSLOCK:
            if (hidDevice && hidDevice->hasKeyboardLedReport() && hidDevice->isCapsLockOn()) break;
            if (elapsed >= waitTimeoutMs) {
                fail("WAIT_FOR_CAPSLOCK timeout");
                return false;
            }
            return false;

        case WaitCondition::BUTTON:
            if (buttonPressedCallback && buttonPressedCallback()) break;
            if (elapsed >= waitTimeoutMs) {
                fail("WAIT_FOR_BUTTON_PRESS timeout");
                return false;
            }
            return false;

        default:
            break;
    }

    waitCondition = WaitCondition::NONE;
    waitDurationMs = 0;
    waitTimeoutMs = 0;
    return true;
}

bool DuckyScriptParser::nextSourceCommandIsWaitForUsb() const {
    if (currentLine < 0 || currentLine >= static_cast<int>(lines.size())) {
        return false;
    }
    String line = trim(lines[currentLine]);
    const int space = line.indexOf(' ');
    String command = space >= 0 ? line.substring(0, space) : line;
    command.toUpperCase();
    return command == "WAIT_FOR_USB";
}

void DuckyScriptParser::process() {
    if (executionComplete || !hidDevice) {
        return;
    }

    if (!updateWaitCondition()) {
        return;
    }
    if (hasError()) {
        return;
    }

    if (repeatRemaining > 0) {
        if (!hidDevice->isConnected() && !repeatLine.startsWith("WAIT_FOR_USB")) {
            fail("HID disconnected during REPEAT");
            return;
        }
        lastProcessedLine = "REPEAT> " + repeatLine;
        executeLineInternal(repeatLine, false);
        repeatRemaining--;
        processedSteps++;
        if (hasError()) {
            executionComplete = true;
            return;
        }
        if (repeatRemaining == 0) {
            repeatLine = "";
        }
        if (!isWaiting() && currentLine >= static_cast<int>(lines.size()) && repeatRemaining == 0) {
            executionComplete = true;
        }
        return;
    }

    if (currentLine >= static_cast<int>(lines.size())) {
        executionComplete = true;
        return;
    }

    if (!hidDevice->isConnected() && !nextSourceCommandIsWaitForUsb()) {
        fail("HID disconnected");
        return;
    }

    lastProcessedLine = lines[currentLine];
    executeLineInternal(lastProcessedLine, true);
    currentLine++;
    processedSteps++;

    if (hasError()) {
        executionComplete = true;
        waitCondition = WaitCondition::NONE;
        return;
    }

    if (!isWaiting() && repeatRemaining == 0 && currentLine >= static_cast<int>(lines.size())) {
        executionComplete = true;
    }
}

String DuckyScriptParser::getCurrentLine() {
    if (currentLine < static_cast<int>(lines.size())) {
        return lines[currentLine];
    }
    return "";
}

String DuckyScriptParser::getLastProcessedLine() const {
    return lastProcessedLine;
}

size_t DuckyScriptParser::getProcessedLineCount() const {
    return processedSteps;
}

size_t DuckyScriptParser::getTotalLineCount() const {
    return totalSteps;
}

bool DuckyScriptParser::isRepeatableCommand(const String& command) const {
    return command == "DELAY" ||
           command == "STRING" ||
           command == "STRINGLN" ||
           command == "STRINGDELAY" || command == "STRING_DELAY" ||
           command == "ALTCHAR" || command == "ALTSTRING" || command == "ALTCODE" ||
           command == "MEDIA" || command == "MOUSE_MOVE" || command == "MOUSEMOVE" ||
           command == "MOUSE_SCROLL" || command == "MOUSESCROLL" ||
           command == "LEFTCLICK" || command == "LEFT_CLICK" || command == "RIGHTCLICK" || command == "RIGHT_CLICK" ||
           command == "MIDDLECLICK" || command == "MIDDLE_CLICK" || command == "WHEELCLICK" || command == "WHEEL_CLICK" ||
           command == "LED" ||
           command == "ALTCHAR" || command == "ALTSTRING" || command == "ALTCODE" ||
           command == "ID" || command == "BT_ID" || command == "BLE_ID" || command == "GLOBE" ||
           command == "WAIT_FOR_BUTTON_PRESS" ||
           command == "HOLD" ||
           command == "RELEASE" || command == "RELEASEALL" || command == "RELEASE_ALL" ||
           modifierForToken(command) != 0 ||
           specialKeys.find(command) != specialKeys.end() ||
           command.startsWith("CTRL-") || command.startsWith("CONTROL-") ||
           command.startsWith("ALT-") || command.startsWith("SHIFT-") ||
           command.startsWith("GUI-") || command.startsWith("WINDOWS-") ||
           command.startsWith("COMMAND-") || command.startsWith("CMD-") ||
           command.length() == 1;
}

void DuckyScriptParser::executeLine(const String& line) {
    executeLineInternal(line, true);
}

void DuckyScriptParser::executeLineInternal(const String& line, bool sourceLine) {
    if (executionComplete || !hidDevice) {
        return;
    }

    String trimmedLine = trim(line);
    if (trimmedLine.length() == 0) {
        return;
    }

    String upperLine = trimmedLine;
    upperLine.toUpperCase();

    if (inCommentBlock) {
        if (upperLine == "REM_BLOCK END") {
            inCommentBlock = false;
        }
        return;
    }

    const int spaceIndex = trimmedLine.indexOf(' ');
    String command = spaceIndex != -1 ? trimmedLine.substring(0, spaceIndex) : trimmedLine;
    String parameters = spaceIndex != -1 ? trimmedLine.substring(spaceIndex + 1) : "";
    command.toUpperCase();
    parameters = trim(parameters);

    if (command == "REM_BLOCK") {
        handleREM_BLOCK(upperLine);
        return;
    }
    if (command == "REM") {
        return;
    }

    uint32_t explicitDelay = 0;
    bool applyDefaultDelay = true;

    if (command == "DELAY") {
        explicitDelay = handleDELAY(parameters);
    } else if (command == "STRING") {
        handleSTRING(parameters);
    } else if (command == "STRINGLN") {
        handleSTRINGLN(parameters);
    } else if (command == "STRINGDELAY" || command == "STRING_DELAY") {
        handleSTRINGDELAY(parameters);
    } else if (command == "DEFAULT_STRING_DELAY" || command == "DEFAULTSTRINGDELAY") {
        handleDEFAULTSTRINGDELAY(parameters);
        applyDefaultDelay = false;
    } else if (command == "DEFAULTDELAY" || command == "DEFAULT_DELAY") {
        handleDEFAULTDELAY(parameters);
        applyDefaultDelay = false;
    } else if (command == "REPEAT") {
        handleREPEAT(parameters);
        applyDefaultDelay = false;
    } else if (command == "WAIT_FOR_USB") {
        handleWAIT_FOR_USB(parameters);
        applyDefaultDelay = false;
    } else if (command == "WAIT_FOR_CAPSLOCK") {
        handleWAIT_FOR_CAPSLOCK(parameters);
        applyDefaultDelay = false;
    } else if (command == "WAIT_FOR_BUTTON_PRESS") {
        scheduleConditionWait(WaitCondition::BUTTON, CONDITION_WAIT_TIMEOUT_MS);
        applyDefaultDelay = false;
    } else if (command == "LED") {
        handleLED(parameters);
    } else if (command == "ALTCHAR") {
        handleALTCHAR(parameters);
    } else if (command == "ALTSTRING" || command == "ALTCODE") {
        handleALTSTRING(parameters);
    } else if (command == "SYSRQ") {
        handleSYSRQ(parameters);
    } else if (command == "MEDIA") {
        handleMEDIA(parameters);
    } else if (command == "MOUSE_MOVE" || command == "MOUSEMOVE" || command == "MOUSE_SCROLL" || command == "MOUSESCROLL" ||
               command == "LEFTCLICK" || command == "LEFT_CLICK" || command == "RIGHTCLICK" || command == "RIGHT_CLICK" ||
               command == "MIDDLECLICK" || command == "MIDDLE_CLICK" || command == "WHEELCLICK" || command == "WHEEL_CLICK") {
        handleMouseCommand(command, parameters);
    } else if (command == "ID" || command == "BT_ID" || command == "BLE_ID" || command == "GLOBE" || command == "DUCKY_LANG") {
        Serial.println(String("Compatibility command accepted: ") + command);
    } else if (command == "HOLD") {
        handleHOLD(parameters);
    } else if (command == "RELEASE" || command == "RELEASEALL" || command == "RELEASE_ALL") {
        handleRELEASE();
    } else if (!handleKeyCombination(command, parameters)) {
        fail("Unknown command: " + command);
    }

    if (!hasError() && sourceLine && isRepeatableCommand(command)) {
        lastRepeatableLine = trimmedLine;
    }

    if (!hasError() && applyDefaultDelay) {
        scheduleWait(explicitDelay + static_cast<uint32_t>(commandDelay));
    }
}

void DuckyScriptParser::handleREM_BLOCK(const String& line) {
    if (line != "REM_BLOCK END") {
        inCommentBlock = true;
    }
}

uint32_t DuckyScriptParser::handleDELAY(const String& line) {
    const uint32_t delayMs = static_cast<uint32_t>(line.toInt());
    Serial.println("Delay scheduled: " + String(delayMs) + "ms");
    return delayMs;
}

void DuckyScriptParser::handleSTRING(const String& line) {
    const uint32_t delayMs = nextStringDelay > 0 ? nextStringDelay : defaultStringDelay;
    hidDevice->sendStringWithDelay(line, delayMs);
    nextStringDelay = 0;
}

void DuckyScriptParser::handleSTRINGLN(const String& line) {
    const uint32_t delayMs = nextStringDelay > 0 ? nextStringDelay : defaultStringDelay;
    hidDevice->sendStringWithDelay(line, delayMs);
    nextStringDelay = 0;
    hidDevice->sendKey(DUCKY_ENTER);
}

void DuckyScriptParser::handleSTRINGDELAY(const String& line) {
    const int split = line.indexOf(' ');
    String delayText = split >= 0 ? line.substring(0, split) : line;
    String text = split >= 0 ? line.substring(split + 1) : String("");
    const uint32_t delayMs = parsePositiveOrDefault(delayText, 1);
    if (delayMs > 2000) {
        fail("STRING_DELAY max is 2000");
        return;
    }
    if (text.length() > 0) hidDevice->sendStringWithDelay(text, delayMs);
    else nextStringDelay = delayMs;
}

void DuckyScriptParser::handleDEFAULTSTRINGDELAY(const String& line) {
    const uint32_t delayMs = line.length() ? static_cast<uint32_t>(line.toInt()) : 0;
    defaultStringDelay = delayMs > 2000 ? 2000 : delayMs;
}

void DuckyScriptParser::handleALTCHAR(const String& line) {
    String digits = line;
    digits.trim();
    if (digits.length() == 0) {
        fail("ALTCHAR requires digits");
        return;
    }
    hidDevice->sendAltCode(digits);
}

void DuckyScriptParser::handleALTSTRING(const String& line) {
    for (size_t i = 0; i < line.length(); ++i) hidDevice->sendAltCode(String(static_cast<uint8_t>(line[i])));
}

void DuckyScriptParser::handleSYSRQ(const String& line) {
    uint8_t key = 0;
    if (!parseKeyToken(line, key)) {
        fail("SYSRQ requires one key");
        return;
    }
    hidDevice->pressKey(DUCKY_PRINT_SCREEN, MOD_ALT_LEFT);
    hidDevice->sendKey(key, MOD_ALT_LEFT);
}

void DuckyScriptParser::handleMEDIA(const String& line) {
    const uint16_t usage = mediaUsageFor(line);
    if (usage == 0) {
        fail("Unknown MEDIA action");
        return;
    }
    hidDevice->consumerPress(usage);
}

void DuckyScriptParser::handleMouseCommand(const String& command, const String& line) {
    if (command == "MOUSE_MOVE" || command == "MOUSEMOVE") {
        int16_t x = 0;
        int16_t y = 0;
        if (!parseSignedPair(line, x, y)) {
            fail("MOUSE_MOVE requires x y");
            return;
        }
        hidDevice->mouseMove(static_cast<int16_t>(x < -2048 ? -2048 : (x > 2048 ? 2048 : x)), static_cast<int16_t>(y < -2048 ? -2048 : (y > 2048 ? 2048 : y)), 0);
        return;
    }
    if (command == "MOUSE_SCROLL" || command == "MOUSESCROLL") {
        const long parsedWheel = line.toInt();
        const int16_t wheel = static_cast<int16_t>(parsedWheel < -512 ? -512 : (parsedWheel > 512 ? 512 : parsedWheel));
        hidDevice->mouseMove(0, 0, wheel);
        return;
    }
    hidDevice->mouseClick(command);
}

bool DuckyScriptParser::handleSafeKey(const String& token) {
    uint8_t key = 0;
    if (!parseKeyToken(token, key)) return false;
    hidDevice->sendKey(key, 0);
    Serial.println("Key: " + token);
    return true;
}

uint8_t DuckyScriptParser::modifierForToken(const String& token) const {
    String value(token);
    value.trim();
    value.toUpperCase();
    if (value == "CTRL" || value == "CONTROL" || value == "LCTRL" || value == "CTRLLEFT") return MOD_CTRL_LEFT;
    if (value == "SHIFT" || value == "LSHIFT" || value == "SHIFTLEFT") return MOD_SHIFT_LEFT;
    if (value == "ALT" || value == "LALT" || value == "ALTLEFT") return MOD_ALT_LEFT;
    if (value == "GUI" || value == "WINDOWS" || value == "WIN" || value == "COMMAND" || value == "CMD" || value == "LGUI") return MOD_GUI_LEFT;
    if (value == "RCTRL" || value == "CTRLRIGHT") return MOD_CTRL_RIGHT;
    if (value == "RSHIFT" || value == "SHIFTRIGHT") return MOD_SHIFT_RIGHT;
    if (value == "RALT" || value == "ALTGR" || value == "ALTRIGHT") return MOD_ALT_RIGHT;
    if (value == "RGUI" || value == "GUIRIGHT") return MOD_GUI_RIGHT;
    return 0;
}

bool DuckyScriptParser::parseKeyToken(const String& token, uint8_t& key) const {
    String value(token);
    value.trim();
    value.toUpperCase();
    auto it = specialKeys.find(value);
    if (it != specialKeys.end()) {
        key = it->second;
        return true;
    }
    if (value.length() == 1) {
        char c = value[0];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        key = static_cast<uint8_t>(c);
        return true;
    }
    return false;
}

bool DuckyScriptParser::handleKeyCombination(const String& command, const String& parameters) {
    String sequence = command;
    if (parameters.length() > 0) sequence += " " + parameters;
    sequence.replace("+", " ");
    sequence.replace("\t", " ");

    String upper(sequence);
    upper.toUpperCase();
    if (upper.startsWith("CTRL-") || upper.startsWith("CONTROL-") ||
        upper.startsWith("ALT-") || upper.startsWith("SHIFT-") ||
        upper.startsWith("GUI-") || upper.startsWith("WINDOWS-") ||
        upper.startsWith("COMMAND-")) {
        sequence.replace("-", " ");
    }

    uint8_t modifiers = 0;
    uint8_t key = 0;
    bool keySeen = false;
    int position = 0;
    while (position < static_cast<int>(sequence.length())) {
        while (position < static_cast<int>(sequence.length()) && sequence[position] == ' ') position++;
        if (position >= static_cast<int>(sequence.length())) break;
        int end = sequence.indexOf(' ', position);
        if (end < 0) end = sequence.length();
        String token = sequence.substring(position, end);
        token.trim();
        position = end + 1;
        if (token.length() == 0) continue;

        const uint8_t modifier = modifierForToken(token);
        if (modifier != 0) {
            modifiers |= modifier;
            continue;
        }
        uint8_t parsedKey = 0;
        if (!parseKeyToken(token, parsedKey) || keySeen) return false;
        key = parsedKey;
        keySeen = true;
    }

    if (!keySeen && modifiers == 0) return false;
    hidDevice->sendKey(keySeen ? key : 0, modifiers);
    Serial.println("Key combination: " + sequence);
    return true;
}

void DuckyScriptParser::handleHOLD(const String& parameters) {
    String mouseToken = parameters;
    mouseToken.toUpperCase();
    mouseToken.replace("_", "");
    if (mouseToken == "LEFT" || mouseToken == "LEFTCLICK" || mouseToken == "RIGHT" || mouseToken == "RIGHTCLICK" ||
        mouseToken == "MIDDLE" || mouseToken == "MIDDLECLICK" || mouseToken == "WHEELCLICK" ||
        mouseToken == "BACK" || mouseToken == "BACKWARD" || mouseToken == "FORWARD") {
        hidDevice->mousePress(mouseToken);
        return;
    }
    String sequence(parameters);
    sequence.replace("+", " ");
    sequence.replace("-", " ");
    uint8_t modifiers = 0;
    uint8_t key = 0;
    bool keySeen = false;
    int position = 0;
    while (position < static_cast<int>(sequence.length())) {
        while (position < static_cast<int>(sequence.length()) && sequence[position] == ' ') position++;
        if (position >= static_cast<int>(sequence.length())) break;
        int end = sequence.indexOf(' ', position);
        if (end < 0) end = sequence.length();
        String token = sequence.substring(position, end);
        token.trim();
        position = end + 1;
        const uint8_t modifier = modifierForToken(token);
        if (modifier) {
            modifiers |= modifier;
            continue;
        }
        uint8_t parsedKey = 0;
        if (!parseKeyToken(token, parsedKey) || keySeen) {
            fail("HOLD expects modifiers and one key");
            return;
        }
        key = parsedKey;
        keySeen = true;
    }
    if (!keySeen && modifiers == 0) {
        fail("HOLD requires a key");
        return;
    }
    hidDevice->pressKey(keySeen ? key : 0, modifiers);
    Serial.println("Hold: " + parameters);
}

void DuckyScriptParser::handleRELEASE() {
    hidDevice->releaseAllKeys();
    Serial.println("Release all keys");
}

void DuckyScriptParser::handleDEFAULTDELAY(const String& line) {
    commandDelay = static_cast<unsigned long>(line.toInt());
    if (commandDelay > 2000) {
        commandDelay = 2000;
    }
    Serial.println("Default delay: " + String(commandDelay) + "ms");
}

void DuckyScriptParser::handleREPEAT(const String& parameters) {
    if (lastRepeatableLine.length() == 0) {
        fail("REPEAT has no previous command");
        return;
    }
    uint32_t count = parsePositiveOrDefault(parameters, 1);
    if (count > MAX_REPEAT_COUNT) {
        fail("REPEAT max is 1000");
        return;
    }
    repeatLine = lastRepeatableLine;
    repeatRemaining = count;
    Serial.println("Repeat scheduled: " + String(count) + " x " + repeatLine);
}

void DuckyScriptParser::handleWAIT_FOR_USB(const String& parameters) {
    const uint32_t timeoutMs = parsePositiveOrDefault(parameters, CONDITION_WAIT_TIMEOUT_MS);
    if (hidDevice->isConnected()) {
        Serial.println("WAIT_FOR_USB: already connected");
        return;
    }
    scheduleConditionWait(WaitCondition::USB, timeoutMs > CONDITION_WAIT_TIMEOUT_MS ? CONDITION_WAIT_TIMEOUT_MS : timeoutMs);
    Serial.println("WAIT_FOR_USB: waiting");
}

void DuckyScriptParser::handleWAIT_FOR_CAPSLOCK(const String& parameters) {
    const uint32_t timeoutMs = parsePositiveOrDefault(parameters, CONDITION_WAIT_TIMEOUT_MS);
    if (hidDevice->hasKeyboardLedReport() && hidDevice->isCapsLockOn()) {
        Serial.println("WAIT_FOR_CAPSLOCK: already ON");
        return;
    }
    scheduleConditionWait(WaitCondition::CAPSLOCK, timeoutMs > CONDITION_WAIT_TIMEOUT_MS ? CONDITION_WAIT_TIMEOUT_MS : timeoutMs);
    Serial.println("WAIT_FOR_CAPSLOCK: waiting for host Caps Lock ON");
}

void DuckyScriptParser::handleLED(const String& parameters) {
    String state(parameters);
    state.toUpperCase();
    if (state != "READY" && state != "WAIT" && state != "RUN" && state != "ERROR" && state != "OFF") {
        fail("LED expects READY/WAIT/RUN/ERROR/OFF");
        return;
    }
    hidDevice->setFeedbackState(state);
    Serial.println("LED feedback: " + state);
}

void DuckyScriptParser::scheduleWait(uint32_t ms) {
    if (ms == 0) {
        waitCondition = WaitCondition::NONE;
        waitDurationMs = 0;
        return;
    }
    waitCondition = WaitCondition::TIME;
    waitStartedMs = millis();
    waitDurationMs = ms;
    waitTimeoutMs = 0;
}

void DuckyScriptParser::scheduleConditionWait(WaitCondition condition, uint32_t timeoutMs) {
    waitCondition = condition;
    waitStartedMs = millis();
    waitDurationMs = 0;
    waitTimeoutMs = timeoutMs;
}

void DuckyScriptParser::fail(const String& message) {
    executionError = message;
    executionErrorLine = static_cast<uint32_t>(currentLine + 1);
    executionComplete = true;
    waitCondition = WaitCondition::NONE;
    repeatRemaining = 0;
    Serial.println("SCRIPT ERROR line " + String(executionErrorLine) + ": " + message);
    if (hidDevice) {
        hidDevice->setFeedbackState("ERROR");
    }
}

bool DuckyScriptParser::isWaiting() const {
    return waitCondition != WaitCondition::NONE;
}

uint32_t DuckyScriptParser::getRemainingWaitMs() const {
    if (waitCondition == WaitCondition::NONE) {
        return 0;
    }
    const uint32_t elapsed = static_cast<uint32_t>(millis() - waitStartedMs);
    const uint32_t target = waitCondition == WaitCondition::TIME ? waitDurationMs : waitTimeoutMs;
    return elapsed >= target ? 0 : target - elapsed;
}

String DuckyScriptParser::getWaitReason() const {
    switch (waitCondition) {
        case WaitCondition::TIME: return "DELAY";
        case WaitCondition::USB: return "USB";
        case WaitCondition::CAPSLOCK: return "CAPSLOCK";
        case WaitCondition::BUTTON: return "BUTTON";
        default: return "";
    }
}

void DuckyScriptParser::stopExecution() {
    executionComplete = true;
    currentLine = 0;
    lines.clear();
    lastProcessedLine = "";
    lastRepeatableLine = "";
    repeatLine = "";
    repeatRemaining = 0;
    processedSteps = 0;
    totalSteps = 0;
    waitCondition = WaitCondition::NONE;
    waitDurationMs = 0;
    waitTimeoutMs = 0;
    executionError = "";
    executionErrorLine = 0;
}

size_t DuckyScriptParser::estimateTotalSteps() const {
    size_t total = lines.size();
    String previousRepeatable;
    bool commentBlock = false;

    for (const String& source : lines) {
        String line = trim(source);
        String upper(line);
        upper.toUpperCase();
        if (commentBlock) {
            if (upper == "REM_BLOCK END") {
                commentBlock = false;
            }
            continue;
        }
        const int space = line.indexOf(' ');
        String command = space >= 0 ? line.substring(0, space) : line;
        String parameters = space >= 0 ? trim(line.substring(space + 1)) : "";
        command.toUpperCase();
        if (command == "REM_BLOCK" && upper != "REM_BLOCK END") {
            commentBlock = true;
            continue;
        }
        if (command == "REPEAT" && previousRepeatable.length() > 0) {
            uint32_t count = parsePositiveOrDefault(parameters, 1);
            if (count > MAX_REPEAT_COUNT) count = MAX_REPEAT_COUNT;
            total += count;
        } else if (isRepeatableCommand(command)) {
            previousRepeatable = line;
        }
    }
    return total;
}

String DuckyScriptParser::trim(const String& str) const {
    int start = 0;
    int end = static_cast<int>(str.length()) - 1;
    while (start <= end && isWhitespace(str[start])) {
        start++;
    }
    while (end >= start && isWhitespace(str[end])) {
        end--;
    }
    return str.substring(start, end + 1);
}

bool DuckyScriptParser::isWhitespace(char c) const {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
