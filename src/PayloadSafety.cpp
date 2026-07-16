#include "PayloadSafety.h"

namespace {
constexpr uint32_t MAX_SCRIPT_LINES = 2048;
constexpr uint32_t MAX_DELAY_MS = 30000;
constexpr uint32_t MAX_DEFAULT_DELAY_MS = 2000;
constexpr uint32_t MAX_DECLARED_WAIT_MS = 600000;
constexpr size_t MAX_TEXT_CHARS_PER_LINE = 500;
constexpr uint32_t MAX_REPEAT_COUNT = 1000;
constexpr uint32_t MAX_CONDITIONAL_WAITS = 32;
}

bool PayloadSafety::allowedCommand(const String& input) {
    String command(input);
    command.toUpperCase();
    if (command.length() == 1) return true;

    const char* fixed[] = {
        "REM", "REM_BLOCK", "DELAY", "DEFAULTDELAY", "DEFAULT_DELAY",
        "STRING", "STRINGLN", "STRINGDELAY", "STRING_DELAY", "DEFAULT_STRING_DELAY", "DEFAULTSTRINGDELAY",
        "REPEAT", "LED", "WAIT_FOR_USB", "WAIT_FOR_CAPSLOCK", "WAIT_FOR_BUTTON_PRESS",
        "ALTCHAR", "ALTSTRING", "ALTCODE", "ID", "BT_ID", "BLE_ID", "GLOBE", "DUCKY_LANG",
        "MEDIA", "MOUSE_MOVE", "MOUSEMOVE", "MOUSE_SCROLL", "MOUSESCROLL",
        "LEFTCLICK", "LEFT_CLICK", "RIGHTCLICK", "RIGHT_CLICK",
        "MIDDLECLICK", "MIDDLE_CLICK", "WHEELCLICK", "WHEEL_CLICK",
        "HOLD", "RELEASE", "RELEASEALL", "RELEASE_ALL",
        "CTRL", "CONTROL", "SHIFT", "ALT", "GUI", "WINDOWS", "WIN",
        "COMMAND", "CMD", "LCTRL", "RCTRL", "LSHIFT", "RSHIFT",
        "LALT", "RALT", "ALTGR", "LGUI", "RGUI",
        "ENTER", "RETURN", "TAB", "SPACE", "BACKSPACE", "BKSP",
        "DELETE", "DEL", "ESC", "ESCAPE", "INSERT", "INS",
        "UP", "UPARROW", "DOWN", "DOWNARROW", "LEFT", "LEFTARROW",
        "RIGHT", "RIGHTARROW", "HOME", "END", "PAGEUP", "PAGE_UP",
        "PAGEDOWN", "PAGE_DOWN", "PGUP", "PGDN", "CAPSLOCK", "CAPS_LOCK",
        "NUMLOCK", "NUM_LOCK", "SCROLLLOCK", "SCROLL_LOCK",
        "PRINTSCREEN", "PRINT_SCREEN", "PRTSC", "SYSRQ", "PAUSE", "BREAK",
        "MENU", "APP", "APPLICATION"
    };
    for (const char* item : fixed) {
        if (command == item) return true;
    }
    if (command[0] == 'F') {
        const int number = command.substring(1).toInt();
        if (number >= 1 && number <= 24 && command == (String("F") + String(number))) return true;
    }
    if (command.startsWith("KP_") || command.startsWith("NUMPAD")) return true;
    return command.startsWith("CTRL-") || command.startsWith("CONTROL-") ||
           command.startsWith("ALT-") || command.startsWith("SHIFT-") ||
           command.startsWith("GUI-") || command.startsWith("WINDOWS-") ||
           command.startsWith("COMMAND-") || command.startsWith("CMD-");
}

bool PayloadSafety::parseUnsigned(const String& value, uint32_t& result) {
    if (value.length() == 0) {
        return false;
    }

    uint64_t parsed = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = parsed * 10ULL + static_cast<uint64_t>(c - '0');
        if (parsed > 0xFFFFFFFFULL) {
            return false;
        }
    }

    result = static_cast<uint32_t>(parsed);
    return true;
}

bool PayloadSafety::validate(const String& script, String& reason, uint32_t initialDefaultDelayMs) {
    int start = 0;
    uint32_t lineNumber = 1;
    uint32_t lineCount = 0;
    bool inCommentBlock = false;
    uint32_t defaultDelay = initialDefaultDelayMs > MAX_DEFAULT_DELAY_MS ? MAX_DEFAULT_DELAY_MS : initialDefaultDelayMs;
    uint64_t declaredWait = 0;
    uint64_t previousRepeatableWait = 0;
    bool previousRepeatable = false;
    uint32_t conditionalWaits = 0;

    while (start <= static_cast<int>(script.length())) {
        int end = script.indexOf('\n', start);
        if (end < 0) {
            end = script.length();
        }

        lineCount++;
        if (lineCount > MAX_SCRIPT_LINES) {
            reason = "too many lines";
            return false;
        }

        String line = script.substring(start, end);
        line.trim();

        if (line.length() > 0) {
            const int space = line.indexOf(' ');
            String command = space >= 0 ? line.substring(0, space) : line;
            String parameters = space >= 0 ? line.substring(space + 1) : "";
            command.toUpperCase();
            parameters.trim();

            String upperLine = line;
            upperLine.toUpperCase();

            if (inCommentBlock) {
                if (upperLine == "REM_BLOCK END") {
                    inCommentBlock = false;
                }
            } else if (command == "REM_BLOCK") {
                if (upperLine != "REM_BLOCK END") {
                    inCommentBlock = true;
                }
            } else if (command == "REM") {

            } else {
                if (!allowedCommand(command)) {
                    reason = "line " + String(lineNumber) + ": " + command;
                    return false;
                }

                if ((command == "STRING" || command == "STRINGLN") &&
                    parameters.length() > MAX_TEXT_CHARS_PER_LINE) {
                    reason = "line " + String(lineNumber) + ": text too long";
                    return false;
                }

                uint64_t thisCommandWait = defaultDelay;
                bool repeatable = false;

                if (command == "DELAY") {
                    uint32_t delayMs = 0;
                    if (!parseUnsigned(parameters, delayMs) || delayMs > MAX_DELAY_MS) {
                        reason = "line " + String(lineNumber) + ": DELAY 0-30000";
                        return false;
                    }
                    thisCommandWait += delayMs;
                    repeatable = true;
                } else if (command == "DEFAULTDELAY" || command == "DEFAULT_DELAY") {
                    uint32_t delayMs = 0;
                    if (!parseUnsigned(parameters, delayMs) || delayMs > MAX_DEFAULT_DELAY_MS) {
                        reason = "line " + String(lineNumber) + ": DEFAULT 0-2000";
                        return false;
                    }
                    defaultDelay = delayMs;
                    thisCommandWait = 0;
                } else if (command == "DEFAULT_STRING_DELAY" || command == "DEFAULTSTRINGDELAY") {
                    uint32_t delayMs = 0;
                    if (!parseUnsigned(parameters, delayMs) || delayMs > MAX_DEFAULT_DELAY_MS) {
                        reason = "line " + String(lineNumber) + ": STRING DELAY 0-2000";
                        return false;
                    }
                    thisCommandWait = 0;
                } else if (command == "STRINGDELAY" || command == "STRING_DELAY") {
                    const int splitAt = parameters.indexOf(' ');
                    String delayText = splitAt >= 0 ? parameters.substring(0, splitAt) : parameters;
                    uint32_t delayMs = 0;
                    if (!parseUnsigned(delayText, delayMs) || delayMs > MAX_DEFAULT_DELAY_MS) {
                        reason = "line " + String(lineNumber) + ": STRING_DELAY 0-2000";
                        return false;
                    }
                    repeatable = true;
                } else if (command == "REPEAT") {
                    if (!previousRepeatable) {
                        reason = "line " + String(lineNumber) + ": REPEAT needs previous command";
                        return false;
                    }
                    uint32_t repeatCount = 1;
                    if (parameters.length() > 0 && !parseUnsigned(parameters, repeatCount)) {
                        reason = "line " + String(lineNumber) + ": REPEAT count";
                        return false;
                    }
                    if (repeatCount == 0 || repeatCount > MAX_REPEAT_COUNT) {
                        reason = "line " + String(lineNumber) + ": REPEAT 1-1000";
                        return false;
                    }
                    thisCommandWait = previousRepeatableWait * repeatCount;
                } else if (command == "WAIT_FOR_USB" || command == "WAIT_FOR_CAPSLOCK") {
                    conditionalWaits++;
                    if (conditionalWaits > MAX_CONDITIONAL_WAITS) {
                        reason = "too many conditional waits";
                        return false;
                    }
                    if (parameters.length() > 0) {
                        uint32_t timeoutMs = 0;
                        if (!parseUnsigned(parameters, timeoutMs) || timeoutMs == 0 || timeoutMs > 30000) {
                            reason = "line " + String(lineNumber) + ": WAIT timeout 1-30000";
                            return false;
                        }
                    }
                    thisCommandWait = 0;
                } else if (command == "WAIT_FOR_BUTTON_PRESS") {
                    thisCommandWait = 0;
                    repeatable = true;
                } else if (command == "MOUSE_MOVE" || command == "MOUSEMOVE") {
                    if (parameters.indexOf(' ') < 0) {
                        reason = "line " + String(lineNumber) + ": MOUSE_MOVE x y";
                        return false;
                    }
                    repeatable = true;
                } else if (command == "MOUSE_SCROLL" || command == "MOUSESCROLL") {
                    if (parameters.length() == 0) {
                        reason = "line " + String(lineNumber) + ": MOUSE_SCROLL value";
                        return false;
                    }
                    repeatable = true;
                } else if (command == "MEDIA") {
                    if (parameters.length() == 0) {
                        reason = "line " + String(lineNumber) + ": MEDIA action";
                        return false;
                    }
                    repeatable = true;
                } else if (command == "HOLD") {
                    if (parameters.length() == 0) {
                        reason = "line " + String(lineNumber) + ": HOLD requires a key";
                        return false;
                    }
                    repeatable = true;
                } else if (command == "RELEASE" || command == "RELEASEALL" || command == "RELEASE_ALL") {
                    repeatable = true;
                } else if (command == "LED") {
                    String state(parameters);
                    state.toUpperCase();
                    if (state != "READY" && state != "WAIT" && state != "RUN" && state != "ERROR" && state != "OFF") {
                        reason = "line " + String(lineNumber) + ": LED state";
                        return false;
                    }
                    repeatable = true;
                } else if (command == "STRING" || command == "STRINGLN" || allowedCommand(command)) {
                    repeatable = true;
                }

                declaredWait += thisCommandWait;
                if (declaredWait > MAX_DECLARED_WAIT_MS) {
                    reason = "declared wait exceeds 600s";
                    return false;
                }

                if (repeatable) {
                    previousRepeatable = true;
                    previousRepeatableWait = thisCommandWait;
                }
            }
        }

        if (end >= static_cast<int>(script.length())) {
            break;
        }
        start = end + 1;
        lineNumber++;
    }

    if (inCommentBlock) {
        reason = "REM_BLOCK missing END";
        return false;
    }

    reason = "OK";
    return true;
}
