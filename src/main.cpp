#include <M5Cardputer.h>
#include <SPI.h>
#include <vector>
#include <esp_system.h>

#include "DuckyScriptParser.h"
#include "DuckyScript3Compiler.h"
#include "MantisUsbDevice.h"
#include "OfficialMSCBridge.h"
#include "SDWholeCardStorage.h"
#include "PayloadManager.h"
#include "PayloadSyncManager.h"
#include "PayloadSafety.h"
#include "MantisConfig.h"
#include "KeyboardLayoutManager.h"
#include "StatusLight.h"
#include "MantisUI.h"

#define LIGHT_ORANGE 0xFD20
#define LIGHT_PURPLE 0xB81F
#define GRAY 0x8410
#define FW_VERSION "MANTIS V1.0"

MantisUsbDevice usbHid;
SDWholeCardStorage sdCard;
OfficialMSCBridge usbMsc;
DuckyScriptParser duckyParser;
DuckyScript3Compiler duckyCompiler;
PayloadManager payloadManager;
PayloadSyncManager payloadSync(payloadManager);
PayloadSyncStats payloadSyncStats{};
MantisConfigManager configManager;
KeyboardLayoutManager keyboardLayouts;
StatusLight statusLight;
MantisUI mantisUi;

const MantisConfig* activeConfig = nullptr;

enum DeviceMode {
    MODE_HOME,
    MODE_PAYLOAD_BROWSER,
    MODE_ARMED_PAYLOAD,
    MODE_OTHER,
    MODE_USB_DRIVE,
    MODE_STATUS,
    MODE_SETTINGS
};

DeviceMode currentMode = MODE_HOME;
bool isExecuting = false;
bool driveEjected = false;
bool sdReady = false;
bool cacheReady = false;
uint32_t visibleCachePayloadCount = 0;
bool bootPreloadAttempted = false;
bool bootPreloadOk = false;
bool lastUsbConnectedState = false;
bool suppressHomeEnterUntilRelease = false;
bool cacheRefreshPending = false;
uint32_t lastTopBarRefreshMs = 0;
bool ejectHandoffPending = false;
uint32_t ejectDetectedMs = 0;
constexpr uint32_t EJECT_IO_QUIET_MS = 180;
constexpr uint32_t EJECT_MAX_WAIT_MS = 1500;

String currentPayload = "";
String currentPayloadPath = "";
String armedPayloadContent = "";
uint32_t armedPayloadSize = 0;
bool armedPayloadReady = false;
uint32_t executionStartedMs = 0;

int homeSelected = 0;
int otherSelected = 0;
int settingsSelected = 0;
MantisConfig settingsDraft{};
bool settingsDraftActive = false;
bool settingsValueEditing = false;
uint8_t settingsInputMask = 0;
constexpr int SETTINGS_VALUE_COUNT = 7;
constexpr int SETTINGS_SAVE_INDEX = 7;
constexpr int SETTINGS_BACK_INDEX = 8;
constexpr int SETTINGS_ITEM_COUNT = 9;
int selectedIndex = 0;
int scrollOffset = 0;
uint32_t lastExecutionDrawMs = 0;

void showBootScreen();
void showHomeMenu();
void showPayloadBrowser();
void showOtherMenu();
void showArmedPayloadScreen();
void showUsbDriveScreen();
void showStatusScreen();
void showSettingsScreen();
void showExecutionScreen();
void showExecutionComplete(bool aborted);
void showScriptError(const String& message, uint32_t line);
void showError(const String& error, uint32_t holdMs = 1200);
void showPayloadSyncResult(const PayloadSyncStats& stats);
void redrawCurrentScreen();
void drawBatteryStatus();
void drawFeedbackDot();
void handleKeyboardInput();
void handleHomeInput();
void handleBrowserInput();
void handleOtherInput();
void handleSettingsInput();
void handleArmedInput();
void moveHome(int delta);
void moveOther(int delta);
void moveSettings(int delta);
void moveBrowser(int delta);
bool armPayloadFromEntry(const FileEntry& entry);
void clearArmedPayload();
void executeArmedPayloadUSB();
void requestPayloadCacheRefresh();
void performPayloadCacheRefresh();
void servicePendingEject();
void showCacheRefreshWaiting();
String formatBytes(uint64_t bytes);
String clippedText(const String& value, size_t maxChars);
String displayPayloadName(const String& name);
String browserPathLabel();
void beginSettingsEdit();
void discardSettingsEdit();
void saveSettingsEdit();
void adjustSelectedSetting(int delta);
void cycleKeyboardLayout(int delta = 1);
void applyConfiguredLayout();
bool payloadButtonPressed();

void syncPhysicalStatusLight(bool force = false);
void applyModeFeedbackState();
uint16_t feedbackColor();
bool uiUsbConnected();
uint8_t currentSettingsInputMask();
bool settingsMatrixKeyPressed(int8_t x, int8_t y);

void showOtherMenu() {
    currentMode = MODE_OTHER;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();

    const char* items[2] = {"Status", "BACK"};
    if (otherSelected < 0) otherSelected = 0;
    if (otherSelected > 1) otherSelected = 1;
    M5Cardputer.Display.setCursor(8, MantisUI::CONTENT_TOP + 4);
    for (int i = 0; i < 2; ++i) {
        M5Cardputer.Display.setTextColor(i == otherSelected ? LIGHT_ORANGE : WHITE, BLACK);
        M5Cardputer.Display.print(i == otherSelected ? "> " : "  ");
        M5Cardputer.Display.println(items[i]);
    }
    mantisUi.drawFooter();
}

void handleOtherInput() {
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        moveOther(-1);
    } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        moveOther(1);
    } else if (M5Cardputer.Keyboard.keysState().enter) {
        if (otherSelected == 0) {
            currentMode = MODE_STATUS;
            showStatusScreen();
        } else {
            currentMode = MODE_HOME;
            showHomeMenu();
        }
    } else if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(27)) {
        currentMode = MODE_HOME;
        showHomeMenu();
    }
}

bool settingsMatrixKeyPressed(int8_t x, int8_t y) {
    const auto& keyList = M5Cardputer.Keyboard.keyList();
    for (const auto& key : keyList) {
        if (key.x == x && key.y == y) return true;
    }
    return false;
}

uint8_t currentSettingsInputMask() {
    const auto keys = M5Cardputer.Keyboard.keysState();
    uint8_t mask = 0;
    if (M5Cardputer.Keyboard.isKeyPressed(';') || keys.up) mask |= 0x01;
    if (M5Cardputer.Keyboard.isKeyPressed('.') || keys.down) mask |= 0x02;

    const bool leftPressed = keys.left || settingsMatrixKeyPressed(10, 3);
    const bool rightPressed = keys.right || settingsMatrixKeyPressed(12, 3);
    if (leftPressed) mask |= 0x04;
    if (rightPressed) mask |= 0x08;

    if (keys.enter) mask |= 0x10;
    if (M5Cardputer.Keyboard.isKeyPressed('`') ||
        M5Cardputer.Keyboard.isKeyPressed(27) ||
        keys.esc) mask |= 0x20;
    return mask;
}

void handleSettingsInput() {
    if (!settingsDraftActive) beginSettingsEdit();

    const uint8_t currentMask = currentSettingsInputMask();
    const uint8_t pressedMask = currentMask & static_cast<uint8_t>(~settingsInputMask);
    settingsInputMask = currentMask;

    if (pressedMask == 0) return;

    const bool up = (pressedMask & 0x01) != 0;
    const bool down = (pressedMask & 0x02) != 0;
    const bool left = (pressedMask & 0x04) != 0;
    const bool right = (pressedMask & 0x08) != 0;
    const bool enter = (pressedMask & 0x10) != 0;
    const bool escape = (pressedMask & 0x20) != 0;

    if (settingsValueEditing) {
        if (escape) {
            settingsValueEditing = false;
            showSettingsScreen();
            return;
        }
        if (left || right) {
            adjustSelectedSetting(left ? -1 : 1);
            showSettingsScreen();
            return;
        }
        if (enter) {
            if (settingsSelected == 6) {

                statusLight.setBrightness(settingsDraft.ledBrightness);
                syncPhysicalStatusLight(false);
            }
            settingsValueEditing = false;
            showSettingsScreen();
            return;
        }
        return;
    }

    if (up) {
        moveSettings(-1);
        return;
    }
    if (down) {
        moveSettings(1);
        return;
    }
    if (escape) {
        discardSettingsEdit();
        currentMode = MODE_HOME;
        showHomeMenu();
        return;
    }

    if (left || right || !enter) return;

    if (settingsSelected < SETTINGS_VALUE_COUNT) {
        settingsValueEditing = true;
        showSettingsScreen();
        return;
    }

    if (settingsSelected == SETTINGS_SAVE_INDEX) {
        saveSettingsEdit();
        showSettingsScreen();
        return;
    }

    discardSettingsEdit();
    suppressHomeEnterUntilRelease = true;
    currentMode = MODE_HOME;
    showHomeMenu();
}
void moveOther(int delta) {
    otherSelected += delta;
    if (otherSelected < 0) otherSelected = 1;
    if (otherSelected > 1) otherSelected = 0;
    showOtherMenu();
}

void moveSettings(int delta) {
    settingsSelected += delta;
    if (settingsSelected < 0) settingsSelected = SETTINGS_ITEM_COUNT - 1;
    if (settingsSelected >= SETTINGS_ITEM_COUNT) settingsSelected = 0;
    showSettingsScreen();
}

void beginSettingsEdit() {
    settingsDraft = configManager.get();
    settingsDraftActive = true;
    settingsValueEditing = false;
    settingsSelected = 0;

    settingsInputMask = currentSettingsInputMask();
}

void discardSettingsEdit() {
    settingsDraft = configManager.get();
    settingsDraftActive = false;
    settingsValueEditing = false;
    M5Cardputer.Display.setBrightness(static_cast<uint8_t>(activeConfig->screenBrightness * 255 / 100));
    statusLight.setEnabled(activeConfig->ledEnabled);
    statusLight.setBrightness(activeConfig->ledBrightness);

    syncPhysicalStatusLight(true);
}

void saveSettingsEdit() {
    if (!configManager.saveInternal(settingsDraft)) {
        showError("Settings save failed");
        return;
    }

    activeConfig = &configManager.get();
    settingsDraft = *activeConfig;
    settingsDraftActive = true;
    settingsValueEditing = false;
    M5Cardputer.Display.setBrightness(static_cast<uint8_t>(activeConfig->screenBrightness * 255 / 100));
    applyConfiguredLayout();
    statusLight.setEnabled(activeConfig->ledEnabled);
    statusLight.setBrightness(activeConfig->ledBrightness);
    syncPhysicalStatusLight(true);

    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.println("SETTINGS SAVED");
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println("Layout: " + configManager.layoutName());
    M5Cardputer.Display.printf("Brightness: %u%%\n", activeConfig->screenBrightness);
    M5Cardputer.Display.printf("Default delay: %lu ms\n", static_cast<unsigned long>(activeConfig->defaultDelayMs));
    M5Cardputer.Display.printf("LED: %s  %u%%\n", activeConfig->ledEnabled ? "ON" : "OFF", activeConfig->ledBrightness);
    M5Cardputer.Display.println("USB: MantisUSB");
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.println("Saved to internal NVS");
}

void adjustSelectedSetting(int delta) {
    switch (settingsSelected) {
        case 0:
            cycleKeyboardLayout(delta);
            break;
        case 1: {
            int value = static_cast<int>(settingsDraft.defaultDelayMs) + (delta * 10);
            if (value < 0) value = 200;
            if (value > 200) value = 0;
            settingsDraft.defaultDelayMs = static_cast<uint32_t>(value);
            break;
        }
        case 2: {
            int value = static_cast<int>(settingsDraft.screenBrightness) + (delta * 10);
            if (value < 10) value = 100;
            if (value > 100) value = 10;
            settingsDraft.screenBrightness = static_cast<uint8_t>(value);
            M5Cardputer.Display.setBrightness(static_cast<uint8_t>(settingsDraft.screenBrightness * 255 / 100));

            syncPhysicalStatusLight(true);
            break;
        }
        case 3:
            settingsDraft.showExecutionProgress = !settingsDraft.showExecutionProgress;
            break;
        case 4:
            settingsDraft.returnToBrowser = !settingsDraft.returnToBrowser;
            break;
        case 5:
            settingsDraft.ledEnabled = !settingsDraft.ledEnabled;
            statusLight.setEnabled(settingsDraft.ledEnabled);
            syncPhysicalStatusLight();
            break;
        case 6: {
            int value = static_cast<int>(settingsDraft.ledBrightness) + (delta * 10);
            if (value < 10) value = 100;
            if (value > 100) value = 10;
            settingsDraft.ledBrightness = static_cast<uint8_t>(value);

            break;
        }
        default:
            break;
    }
}
void cycleKeyboardLayout(int delta) {
    const size_t count = keyboardLayouts.count();
    if (count == 0) {
        settingsDraft.keyboardLayoutName = "US";
        return;
    }
    int current = keyboardLayouts.indexOf(settingsDraft.keyboardLayoutName);
    if (current < 0) current = keyboardLayouts.indexOf("US");
    current += delta < 0 ? -1 : 1;
    if (current < 0) current = static_cast<int>(count) - 1;
    if (current >= static_cast<int>(count)) current = 0;
    settingsDraft.keyboardLayoutName = keyboardLayouts.nameAt(static_cast<size_t>(current));
}

void applyConfiguredLayout() {
    if (!activeConfig) return;
    const String selected = keyboardLayouts.normalizedName(activeConfig->keyboardLayoutName);
    usbHid.setKeyboardLayout(selected, keyboardLayouts.tableFor(selected));
}

bool payloadButtonPressed() {
    return M5Cardputer.Keyboard.keysState().enter;
}


void applyModeFeedbackState() {

    if (!sdReady) {
        usbHid.setFeedbackState("ERROR");
        return;
    }
    if (isExecuting) {
        usbHid.setFeedbackState("RUN");
        return;
    }
    usbHid.setFeedbackState(uiUsbConnected() ? "READY" : "WAIT");
}

void syncPhysicalStatusLight(bool force) {

    statusLight.setColor565(feedbackColor());
    if (force) {
        statusLight.writeCurrent();
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setBrightness(255);
    M5Cardputer.Display.clear();
    mantisUi.resetTopBarCache();
    showBootScreen();

    Serial.begin(115200);
    statusLight.begin();
    usbHid.setFeedbackState("WAIT");
    syncPhysicalStatusLight();

    Serial.println("MantisHID+MSC public version 1.0");
    Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));

    sdReady = sdCard.begin();
    if (sdReady) {
        keyboardLayouts.loadFromWholeSd(sdCard);
        configManager.loadFromWholeSd(sdCard);
    } else {
        Serial.printf("MANTISUSB.IMG initialization failed: %s\n", sdCard.lastError());
    }

    cacheReady = payloadManager.begin();
    if (!cacheReady) {
        Serial.println("Payload cache unavailable");
    }
    visibleCachePayloadCount = cacheReady ? payloadManager.countPayloadFiles() : 0;

    configManager.loadInternalOverride();
    MantisConfig normalizedConfig = configManager.get();
    normalizedConfig.keyboardLayoutName = keyboardLayouts.normalizedName(normalizedConfig.keyboardLayoutName);
    configManager.apply(normalizedConfig);
    activeConfig = &configManager.get();
    M5Cardputer.Display.setBrightness(static_cast<uint8_t>(activeConfig->screenBrightness * 255 / 100));
    statusLight.setEnabled(activeConfig->ledEnabled);
    statusLight.setBrightness(activeConfig->ledBrightness);
    syncPhysicalStatusLight();

    bootPreloadAttempted = sdReady && cacheReady;
    if (bootPreloadAttempted) {
        payloadSyncStats = payloadSync.syncFromWholeSd(sdCard);
        bootPreloadOk = payloadSyncStats.ok;
        payloadManager.returnToRoot();
        payloadManager.refresh();

        visibleCachePayloadCount = payloadManager.countPayloadFiles();
        if (payloadSyncStats.ok && visibleCachePayloadCount != payloadSyncStats.filesCopied) {
            bootPreloadOk = false;
            Serial.printf(
                "CACHE PRELOAD COUNT MISMATCH: copied=%lu actual=%lu\n",
                static_cast<unsigned long>(payloadSyncStats.filesCopied),
                static_cast<unsigned long>(visibleCachePayloadCount)
            );
        }
    }

    Serial.printf(
        "CACHE PRELOAD: attempted=%s ok=%s files=%lu backend=%s config=%s\n",
        bootPreloadAttempted ? "YES" : "NO",
        bootPreloadOk ? "YES" : "NO",
        static_cast<unsigned long>(visibleCachePayloadCount),
        payloadManager.cacheBackendName(),
        configManager.sourceLabel().c_str()
    );

    if (!usbMsc.begin(sdReady ? &sdCard : nullptr)) {
        showError("USB MSC Config Error", 1500);
        return;
    }

    if (sdReady) {
        Serial.printf(
            "MSC FAST PRE-USB READY: sectors=%lu sectorSize=%u capacity=%llu MiB\n",
            static_cast<unsigned long>(sdCard.sectorCount()),
            static_cast<unsigned>(sdCard.sectorSize()),
            sdCard.capacityBytes() / (1024ULL * 1024ULL)
        );
    } else {
        Serial.println("MSC FAST PRE-USB: MANTISUSB.IMG unavailable");
    }

    usbHid.setProductName("MantisUSB");
    applyConfiguredLayout();
    duckyParser.setButtonCallback(payloadButtonPressed);
    usbHid.setFeedbackState("READY");
    syncPhysicalStatusLight();
    if (!usbHid.begin()) {
        showError("USB HID Error", 1500);
        return;
    }

    currentMode = MODE_HOME;
    driveEjected = false;
    homeSelected = 0;
    otherSelected = 0;
    settingsSelected = 0;
    selectedIndex = 0;
    scrollOffset = 0;
    lastUsbConnectedState = usbHid.isConnected();
    clearArmedPayload();
    showHomeMenu();

    Serial.printf("Setup complete at %lu ms\n", millis());
}

void loop() {
    M5Cardputer.update();

    if (suppressHomeEnterUntilRelease && !M5Cardputer.Keyboard.keysState().enter) {
        suppressHomeEnterUntilRelease = false;
    }

    if (sdReady) {
        sdCard.service();
    }

    if (usbMsc.consumeEjected()) {
        Serial.println("MSC EJECT EVENT: deferred handoff");
        ejectHandoffPending = true;
        ejectDetectedMs = millis();
    }

    if (ejectHandoffPending) {
        servicePendingEject();
        if (ejectHandoffPending) return;
    }

    const bool usbConnectedNow = usbHid.isConnected();
    if (usbConnectedNow != lastUsbConnectedState) {
        Serial.printf("USB HOST STATE: %s\n", usbConnectedNow ? "CONNECTED" : "DISCONNECTED");
        lastUsbConnectedState = usbConnectedNow;

        if (!isExecuting) {

            applyModeFeedbackState();
            redrawCurrentScreen();
            syncPhysicalStatusLight(true);
        }
    }

    applyModeFeedbackState();
    syncPhysicalStatusLight(false);
    statusLight.writeCurrent();

    if (millis() - lastTopBarRefreshMs >= 1000U) {
        lastTopBarRefreshMs = millis();
        mantisUi.updateTopBar(FW_VERSION, feedbackColor());
    }

    if (isExecuting) {
        if (M5Cardputer.Keyboard.isPressed() &&
            (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(27))) {
            duckyParser.stopExecution();
            isExecuting = false;
            showExecutionComplete(true);
                return;
        }

        duckyParser.process();

        if (duckyParser.hasError()) {
            const String parserError = duckyParser.getError();
            const uint32_t parserLine = duckyParser.getErrorLine();
            isExecuting = false;
            usbHid.setFeedbackState("ERROR");
            showScriptError(parserError, parserLine);
            return;
        }

        if (activeConfig->showExecutionProgress && millis() - lastExecutionDrawMs >= 60) {
            lastExecutionDrawMs = millis();
            M5Cardputer.Display.fillRect(0, 56, M5Cardputer.Display.width(), 54, BLACK);
            M5Cardputer.Display.setCursor(0, 56);
            if (duckyParser.isWaiting()) {
                M5Cardputer.Display.setTextColor(YELLOW);
                M5Cardputer.Display.println("WAIT: " + duckyParser.getWaitReason());
                M5Cardputer.Display.printf("Remaining: %lu ms\n",
                    static_cast<unsigned long>(duckyParser.getRemainingWaitMs()));
            } else {
                M5Cardputer.Display.setTextColor(GREEN);
                M5Cardputer.Display.println("> " + clippedText(duckyParser.getLastProcessedLine(), 34));
            }
            M5Cardputer.Display.setTextColor(WHITE);
            M5Cardputer.Display.printf("Progress: %u/%u\n",
                static_cast<unsigned>(duckyParser.getProcessedLineCount()),
                static_cast<unsigned>(duckyParser.getTotalLineCount()));
            M5Cardputer.Display.println(usbMsc.isSharing() ? "MANTISUSB: ONLINE" : "MANTISUSB: EJECTED");
            drawFeedbackDot();
        }

        if (duckyParser.isExecutionComplete()) {
            isExecuting = false;
            showExecutionComplete(false);
        }
        return;
    }

    if (M5Cardputer.BtnA.isPressed()) {
        currentMode = MODE_STATUS;
        showStatusScreen();
        return;
    }

    if (currentMode == MODE_SETTINGS) {
        handleSettingsInput();
        return;
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {

        handleKeyboardInput();
        statusLight.writeCurrent();
    }
}

void handleKeyboardInput() {
    if (currentMode == MODE_ARMED_PAYLOAD) {
        handleArmedInput();
        return;
    }
    if (currentMode == MODE_SETTINGS) {
        handleSettingsInput();
        return;
    }
    if (currentMode == MODE_OTHER) {
        handleOtherInput();
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
        currentMode = MODE_STATUS;
        showStatusScreen();
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
        requestPayloadCacheRefresh();
        return;
    }

    switch (currentMode) {
        case MODE_HOME:
            handleHomeInput();
            break;
        case MODE_PAYLOAD_BROWSER:
            handleBrowserInput();
            break;
        case MODE_USB_DRIVE:
            if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(27)) {
                if (cacheRefreshPending) {
                    cacheRefreshPending = false;
                    showUsbDriveScreen();
                } else {
                    currentMode = MODE_HOME;
                    showHomeMenu();
                }
                    }
            break;
        case MODE_STATUS:
            if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(27)) {
                currentMode = MODE_OTHER;
                showOtherMenu();
                    }
            break;
        default:
            break;
    }
}

void handleHomeInput() {
    const auto keys = M5Cardputer.Keyboard.keysState();
    const bool leftPressed = keys.left || settingsMatrixKeyPressed(10, 3);
    const bool rightPressed = keys.right || settingsMatrixKeyPressed(12, 3);
    if (M5Cardputer.Keyboard.isKeyPressed(';') || keys.up || leftPressed) {
        moveHome(-1);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.') || keys.down || rightPressed) {
        moveHome(1);
        return;
    }
    if (!keys.enter || suppressHomeEnterUntilRelease) return;

    switch (homeSelected) {
        case 0:
            if (cacheReady) {
                payloadManager.returnToRoot();
                selectedIndex = 0;
                scrollOffset = 0;
                currentMode = MODE_PAYLOAD_BROWSER;
                showPayloadBrowser();
            } else {
                showError("Payload cache unavailable");
                showHomeMenu();
            }
            break;
        case 1:
            beginSettingsEdit();
            currentMode = MODE_SETTINGS;
            showSettingsScreen();
            break;
        case 2:
            otherSelected = 0;
            currentMode = MODE_OTHER;
            showOtherMenu();
            break;
        case 3:
            currentMode = MODE_USB_DRIVE;
            showUsbDriveScreen();
            break;
        default:
            break;
    }
}

void handleBrowserInput() {
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        moveBrowser(-1);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        moveBrowser(1);
        return;
    }

    if (M5Cardputer.Keyboard.keysState().enter) {
        const std::vector<FileEntry> files = payloadManager.getFileList();
        const int backIndex = static_cast<int>(files.size());

        if (selectedIndex == backIndex) {
            if (payloadManager.getRelativePath() == "/") {
                currentMode = MODE_HOME;
                showHomeMenu();
            } else {
                payloadManager.navigateUp();
                selectedIndex = 0;
                scrollOffset = 0;
                showPayloadBrowser();
            }
                return;
        }

        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(files.size())) {
            const FileEntry& entry = files[selectedIndex];
            if (entry.isDir) {
                if (payloadManager.navigateDown(entry.name)) {
                    selectedIndex = 0;
                    scrollOffset = 0;
                    showPayloadBrowser();
                }
            } else if (armPayloadFromEntry(entry)) {
                currentMode = MODE_ARMED_PAYLOAD;
                showArmedPayloadScreen();
            }
        }
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(27)) {
        if (payloadManager.getRelativePath() == "/") {
            currentMode = MODE_HOME;
            showHomeMenu();
        } else {
            payloadManager.navigateUp();
            selectedIndex = 0;
            scrollOffset = 0;
            showPayloadBrowser();
        }
    }
}

void handleArmedInput() {
    if (M5Cardputer.Keyboard.keysState().enter) {
        if (usbHid.isConnected()) {
            executeArmedPayloadUSB();
        } else {
            showArmedPayloadScreen();
        }
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(27)) {
        clearArmedPayload();
        currentMode = MODE_PAYLOAD_BROWSER;
        usbHid.setFeedbackState("READY");
        showPayloadBrowser();
    }
}

void moveHome(int delta) {
    homeSelected += delta;
    if (homeSelected < 0) homeSelected = 3;
    if (homeSelected > 3) homeSelected = 0;
    showHomeMenu();
}

void moveBrowser(int delta) {
    const int count = static_cast<int>(payloadManager.getFileList().size()) + 1;
    if (count <= 0) return;
    selectedIndex += delta;
    if (selectedIndex < 0) selectedIndex = count - 1;
    if (selectedIndex >= count) selectedIndex = 0;
    showPayloadBrowser();
}

bool armPayloadFromEntry(const FileEntry& entry) {
    if (entry.isDir) return false;

    const String payloadContent = payloadManager.loadFile(entry.name);
    if (payloadContent.isEmpty()) {
        showError("Empty/Failed Load");
        showPayloadBrowser();
        return false;
    }

    String compiledPayload;
    String compileError;
    uint32_t compileErrorLine = 0;
    if (!duckyCompiler.compile(payloadContent, compiledPayload, compileError, compileErrorLine)) {
        showError(String("DS3 line ") + String(compileErrorLine) + ": " + compileError, 1800);
        showPayloadBrowser();
        return false;
    }

    String blockedReason;
    if (!PayloadSafety::validate(compiledPayload, blockedReason, activeConfig->defaultDelayMs)) {
        showError(String("Safe HID blocked: ") + blockedReason, 1600);
        showPayloadBrowser();
        return false;
    }

    armedPayloadContent = compiledPayload;
    armedPayloadSize = entry.size;
    armedPayloadReady = true;
    currentPayload = entry.name;
    currentPayloadPath = payloadManager.getRelativePath();
    if (!currentPayloadPath.endsWith("/")) currentPayloadPath += "/";
    currentPayloadPath += entry.name;

    Serial.println("PAYLOAD ARMED: " + currentPayloadPath);
    Serial.printf("PAYLOAD COMPILED BYTES: %lu\n", static_cast<unsigned long>(armedPayloadContent.length()));
    return true;
}

void clearArmedPayload() {
    armedPayloadContent = "";
    armedPayloadSize = 0;
    armedPayloadReady = false;
}

void executeArmedPayloadUSB() {
    if (!armedPayloadReady || armedPayloadContent.isEmpty()) {
        showError("No Armed Payload");
        currentMode = MODE_PAYLOAD_BROWSER;
        showPayloadBrowser();
        return;
    }
    if (!usbHid.isConnected()) {
        showArmedPayloadScreen();
        return;
    }

    const String payloadContent = armedPayloadContent;
    clearArmedPayload();

    usbHid.setMode(HID_MODE_KEYBOARD);
    usbHid.setFeedbackState("RUN");

    if (!usbHid.ensureCapsLockOffFast(250)) {
        Serial.println("AUTO CAPS OFF: continuing without confirmed OFF state");
    }

    duckyParser.setHIDDevice(&usbHid);
    duckyParser.setInitialDefaultDelay(activeConfig->defaultDelayMs);
    duckyParser.execute(payloadContent);

    if (duckyParser.hasError()) {
        showScriptError(duckyParser.getError(), duckyParser.getErrorLine());
        return;
    }

    executionStartedMs = millis();
    lastExecutionDrawMs = 0;
    isExecuting = true;
    showExecutionScreen();
}

void servicePendingEject() {
    const uint32_t elapsed = static_cast<uint32_t>(millis() - ejectDetectedMs);
    const bool ioQuiet = usbMsc.lastIoAgeMs() >= EJECT_IO_QUIET_MS;
    if (!ioQuiet && elapsed < EJECT_MAX_WAIT_MS) {
        return;
    }

    const bool detached = usbMsc.detachDrive();
    driveEjected = detached;
    ejectHandoffPending = false;
    Serial.printf("MSC deferred detach=%s quiet=%s elapsed=%lu\n",
                  detached ? "OK" : "FAIL",
                  ioQuiet ? "YES" : "TIMEOUT",
                  static_cast<unsigned long>(elapsed));

    if (!detached) {
        cacheRefreshPending = false;
        showError("USB Drive detach failed", 800);
        redrawCurrentScreen();
        return;
    }

    if (cacheRefreshPending) {
        performPayloadCacheRefresh();
        return;
    }

    redrawCurrentScreen();
}

void showCacheRefreshWaiting() {
    currentMode = MODE_USB_DRIVE;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(5, MantisUI::CONTENT_TOP + 8);
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.println("Eject MANTISUSB on the PC");
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.println("");
    M5Cardputer.Display.println("The refresh starts automatically");
    M5Cardputer.Display.println("after Windows flushes the drive.");
    M5Cardputer.Display.println("");
    M5Cardputer.Display.println("This prevents stale payloads.");
    mantisUi.drawFooter("ESC-cancel");
}

void requestPayloadCacheRefresh() {
    ejectHandoffPending = false;
    if (!sdReady || !cacheReady) {
        showError("SD/cache unavailable");
        redrawCurrentScreen();
        return;
    }

    if (usbMsc.isSharing() && uiUsbConnected()) {
        cacheRefreshPending = true;
        showCacheRefreshWaiting();
        return;
    }

    if (usbMsc.isSharing()) {
        const bool detached = usbMsc.detachDrive();
        driveEjected = detached;
        if (!detached) {
            showError("USB Drive detach failed", 1400);
            redrawCurrentScreen();
            return;
        }
    }

    performPayloadCacheRefresh();
}

void performPayloadCacheRefresh() {
    cacheRefreshPending = false;

    if (!sdReady || !cacheReady) {
        showError("SD/cache unavailable");
        redrawCurrentScreen();
        return;
    }

    if (usbMsc.isSharing()) {

        cacheRefreshPending = true;
        showCacheRefreshWaiting();
        return;
    }

    if (!usbMsc.reclaimDrive()) {
        usbHid.setFeedbackState("ERROR");
        showError("SD sync before cache failed", 1600);
        redrawCurrentScreen();
        return;
    }

    usbHid.setFeedbackState("WAIT");
    mantisUi.drawSectionHeader("PAYLOAD SYNC", FW_VERSION, feedbackColor());
    M5Cardputer.Display.setCursor(5, MantisUI::CONTENT_TOP + 18);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.println("Reading /MantisSD/Payloads...");
    M5Cardputer.Display.println("Clearing stale cache entries...");
    M5Cardputer.Display.println("Do not remove the microSD.");

    payloadSyncStats = payloadSync.syncFromWholeSd(sdCard);
    const bool layoutsReloaded = keyboardLayouts.loadFromWholeSd(sdCard);
    if (!layoutsReloaded) {
        Serial.println("LAYOUT SCAN FAILED: /MantisSD/Layouts");
    }
    if (activeConfig) {
        MantisConfig refreshedConfig = *activeConfig;
        refreshedConfig.keyboardLayoutName = keyboardLayouts.normalizedName(refreshedConfig.keyboardLayoutName);
        configManager.apply(refreshedConfig);
        activeConfig = &configManager.get();
        applyConfiguredLayout();
    }
    payloadManager.returnToRoot();
    payloadManager.refresh();

    const uint32_t verifiedCount = payloadManager.countPayloadFiles();
    if (payloadSyncStats.ok && verifiedCount != payloadSyncStats.filesCopied) {
        payloadSyncStats.ok = false;
        payloadSyncStats.error = "cache verification count mismatch";
    }
    visibleCachePayloadCount = verifiedCount;
    selectedIndex = 0;
    scrollOffset = 0;
    clearArmedPayload();

    Serial.printf(
        "CACHE VERIFIED: visible=%lu copied=%lu sync=%s\n",
        static_cast<unsigned long>(verifiedCount),
        static_cast<unsigned long>(payloadSyncStats.filesCopied),
        payloadSyncStats.ok ? "OK" : "FAIL"
    );

    const bool sharedAgain = usbMsc.shareDrive(true);
    driveEjected = !sharedAgain;
    if (!sharedAgain) {
        usbHid.setFeedbackState("ERROR");
        showError("USB MSC re-share failed", 1600);
        redrawCurrentScreen();
        return;
    }

    if (payloadSyncStats.ok) {
        bootPreloadOk = true;
        applyModeFeedbackState();
    } else {
        usbHid.setFeedbackState("ERROR");
    }

    currentMode = MODE_USB_DRIVE;
    showPayloadSyncResult(payloadSyncStats);
}

void showPayloadSyncResult(const PayloadSyncStats& stats) {
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(stats.ok ? GREEN : RED);
    M5Cardputer.Display.println(stats.ok ? "=== SYNC COMPLETE ===" : "=== SYNC FAILED ===");
    M5Cardputer.Display.setTextColor(WHITE);
    if (stats.ok) {
        M5Cardputer.Display.printf("Payloads: %lu\n", static_cast<unsigned long>(stats.filesCopied));
        M5Cardputer.Display.printf("Layouts: %u\n", static_cast<unsigned>(keyboardLayouts.count()));
        M5Cardputer.Display.println("Cache: " + formatBytes(stats.bytesCopied));
        M5Cardputer.Display.printf("Skipped: %lu\n", static_cast<unsigned long>(stats.skippedFiles));
        M5Cardputer.Display.setTextColor(CYAN);
        M5Cardputer.Display.println("MANTISUSB online again");
    } else {
        M5Cardputer.Display.println(clippedText(stats.error, 38));
    }
}

void showBootScreen() {
    mantisUi.drawSplash(FW_VERSION, YELLOW);
}

void showHomeMenu() {
    currentMode = MODE_HOME;
    applyModeFeedbackState();
    if (!cacheReady) visibleCachePayloadCount = 0;
    mantisUi.drawHome(static_cast<uint8_t>(homeSelected), FW_VERSION, feedbackColor());
}

void showPayloadBrowser() {
    currentMode = MODE_PAYLOAD_BROWSER;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.println(browserPathLabel());
    M5Cardputer.Display.setTextColor(GRAY);
    M5Cardputer.Display.println("------------------------------");

    const std::vector<FileEntry> files = payloadManager.getFileList();
    const int realCount = static_cast<int>(files.size());
    const int totalCount = realCount + 1;
    const int maxItems = 7;

    if (selectedIndex < 0) selectedIndex = 0;
    if (selectedIndex >= totalCount) selectedIndex = totalCount - 1;
    if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + maxItems) scrollOffset = selectedIndex - maxItems + 1;
    if (scrollOffset < 0) scrollOffset = 0;

    for (int i = scrollOffset; i < totalCount && i < scrollOffset + maxItems; ++i) {
        M5Cardputer.Display.setTextColor(i == selectedIndex ? LIGHT_ORANGE : WHITE);
        M5Cardputer.Display.print(i == selectedIndex ? "> " : "  ");
        if (i == realCount) {
            M5Cardputer.Display.println("BACK");
        } else {
            M5Cardputer.Display.print(files[i].isDir ? "[DIR] " : "");
            M5Cardputer.Display.println(clippedText(
                files[i].isDir ? files[i].name : displayPayloadName(files[i].name),
                files[i].isDir ? 28 : 32
            ));
        }
    }

    M5Cardputer.Display.setCursor(0, M5Cardputer.Display.height() - 20);
    M5Cardputer.Display.setTextColor(GRAY);
    if (selectedIndex < realCount) {
        M5Cardputer.Display.printf("%d / %d  %s\n",
            selectedIndex + 1,
            realCount,
            files[selectedIndex].isDir ? "folder" : formatBytes(files[selectedIndex].size).c_str());
    } else {
        M5Cardputer.Display.println("");
    }
    M5Cardputer.Display.println("ESC-back  ENTER-open");
}

void showArmedPayloadScreen() {
    currentMode = MODE_ARMED_PAYLOAD;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(LIGHT_PURPLE);
    M5Cardputer.Display.println("PAYLOAD ARMED");
    M5Cardputer.Display.println("");
    M5Cardputer.Display.setTextColor(LIGHT_ORANGE);
    M5Cardputer.Display.println(clippedText(displayPayloadName(currentPayload), 35));
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println(formatBytes(armedPayloadSize));
    M5Cardputer.Display.println("");

    if (uiUsbConnected()) {
        M5Cardputer.Display.setTextColor(GREEN);
        M5Cardputer.Display.println("USB: READY");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.println("[ ENTER ] INJECT");
        M5Cardputer.Display.println("[ ESC ] CANCEL");
    } else {
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.println("USB: DISCONNECTED");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.println("Connect USB");
        M5Cardputer.Display.println("then press ENTER");
        M5Cardputer.Display.setTextColor(GRAY);
        M5Cardputer.Display.println("ESC: cancel");
    }
    drawFeedbackDot();
}

void showUsbDriveScreen() {
    currentMode = MODE_USB_DRIVE;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP + 2);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println("Name: " + String("MantisUSB"));
    M5Cardputer.Display.println("Capacity: " + formatBytes(sdReady ? sdCard.capacityBytes() : 0));
    M5Cardputer.Display.println(String("MSC: ") + (usbMsc.isSharing() ? "ONLINE" : (driveEjected ? "EJECTED" : "OFFLINE")));
    M5Cardputer.Display.println(String("USB: ") + (uiUsbConnected() ? "CONNECTED" : "DISCONNECTED"));
    M5Cardputer.Display.println(String("SD: ") + (sdReady ? "READY" : "ERROR"));
    M5Cardputer.Display.setTextColor(LIGHT_PURPLE);
    M5Cardputer.Display.println("R = sync payload cache");
    M5Cardputer.Display.setTextColor(GRAY);
    M5Cardputer.Display.println("Then eject MANTISUSB on PC");

    M5Cardputer.Display.setCursor(0, M5Cardputer.Display.height() - 10);
    M5Cardputer.Display.setTextColor(GRAY);
    M5Cardputer.Display.print("ESC-back");
}

void showStatusScreen() {
    currentMode = MODE_STATUS;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP + 2);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println(String("USB: ") + (uiUsbConnected() ? "CONNECTED" : "DISCONNECTED"));
    M5Cardputer.Display.println(String("MSC: ") + (usbMsc.isSharing() ? "ONLINE" : (driveEjected ? "EJECTED" : "OFFLINE")));
    M5Cardputer.Display.println(String("SD: ") + (sdReady ? "READY" : "ERROR"));
    M5Cardputer.Display.printf("CACHE: %lu payloads\n",
        static_cast<unsigned long>(visibleCachePayloadCount));
    M5Cardputer.Display.println(String("PRELOAD: ") + (bootPreloadOk ? "OK" : "FALLBACK"));
    M5Cardputer.Display.println("LAYOUT: " + configManager.layoutName());
    M5Cardputer.Display.printf("LAYOUTS: %u\n", static_cast<unsigned>(keyboardLayouts.count()));
    M5Cardputer.Display.println("CONFIG: " + clippedText(configManager.sourceLabel(), 27));
    M5Cardputer.Display.println(String("CAPS LED: ") +
        (usbHid.hasKeyboardLedReport() ? (usbHid.isCapsLockOn() ? "ON" : "OFF") : "NO REPORT"));

    M5Cardputer.Display.setCursor(0, M5Cardputer.Display.height() - 10);
    M5Cardputer.Display.setTextColor(GRAY);
    M5Cardputer.Display.print("ESC-back");
}

void showSettingsScreen() {
    currentMode = MODE_SETTINGS;
    if (!settingsDraftActive) beginSettingsEdit();
    applyModeFeedbackState();

    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP + 2);

    String labels[SETTINGS_ITEM_COUNT] = {
        "Layout      " + settingsDraft.keyboardLayoutName,
        "Delay       " + String(settingsDraft.defaultDelayMs) + " ms",
        "Bright      " + String(settingsDraft.screenBrightness) + "%",
        String("Progress    ") + (settingsDraft.showExecutionProgress ? "ON" : "OFF"),
        String("Return      ") + (settingsDraft.returnToBrowser ? "ON" : "OFF"),
        String("LED         ") + (settingsDraft.ledEnabled ? "ON" : "OFF"),
        "LED Bright  " + String(settingsDraft.ledBrightness) + "%",
        "SAVE",
        "BACK"
    };

    for (int i = 0; i < SETTINGS_ITEM_COUNT; ++i) {
        const bool selected = i == settingsSelected;
        const bool editing = selected && settingsValueEditing && i < SETTINGS_VALUE_COUNT;
        M5Cardputer.Display.setTextColor(selected ? LIGHT_ORANGE : WHITE);
        M5Cardputer.Display.print(editing ? "* " : (selected ? "> " : "  "));
        M5Cardputer.Display.println(labels[i]);
    }

    M5Cardputer.Display.setCursor(0, M5Cardputer.Display.height() - 10);
    M5Cardputer.Display.setTextColor(GRAY);
    if (settingsValueEditing) {
        M5Cardputer.Display.print("FN+, left  FN+/ right ENTER");
    } else {
        M5Cardputer.Display.print("ENTER-edit  ;/. move");
    }
}

void showExecutionScreen() {
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(BLUE);
    M5Cardputer.Display.println("INJECTING PAYLOAD");
    M5Cardputer.Display.setTextColor(LIGHT_ORANGE);
    M5Cardputer.Display.println(clippedText(displayPayloadName(currentPayload), 35));
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.println("Source: CACHE");
    M5Cardputer.Display.println("Layout: " + configManager.layoutName());
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.println("USB: READY");
    M5Cardputer.Display.setTextColor(GRAY);
    M5Cardputer.Display.println("ESC: abort");
    drawFeedbackDot();
}

void showExecutionComplete(bool aborted) {
    const uint32_t elapsedMs = millis() - executionStartedMs;
    applyModeFeedbackState();
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(aborted ? YELLOW : GREEN);
    M5Cardputer.Display.println(aborted ? "ABORTED" : "COMPLETE");
    M5Cardputer.Display.setTextColor(LIGHT_ORANGE);
    M5Cardputer.Display.println(clippedText(displayPayloadName(currentPayload), 35));
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.printf("Elapsed: %lu ms\n", static_cast<unsigned long>(elapsedMs));
    M5Cardputer.Display.println("Source: CACHE");
    M5Cardputer.Display.println(usbMsc.isSharing() ? "MANTISUSB: ONLINE" : "MANTISUSB: EJECTED");
    drawFeedbackDot();
    delay(850);

    if (activeConfig->returnToBrowser && cacheReady) {
        currentMode = MODE_PAYLOAD_BROWSER;
        showPayloadBrowser();
    } else {
        currentMode = MODE_HOME;
        showHomeMenu();
    }
}

void showScriptError(const String& message, uint32_t line) {
    usbHid.setFeedbackState("ERROR");
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(RED);
    M5Cardputer.Display.println("SCRIPT ERROR");
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.printf("Line: %lu\n", static_cast<unsigned long>(line));
    M5Cardputer.Display.println(clippedText(message, 38));
    M5Cardputer.Display.println("Source: CACHE");
    drawFeedbackDot();
    delay(1500);
    usbHid.setFeedbackState("READY");

    if (activeConfig->returnToBrowser && cacheReady) {
        currentMode = MODE_PAYLOAD_BROWSER;
        showPayloadBrowser();
    } else {
        currentMode = MODE_HOME;
        showHomeMenu();
    }
}

void showError(const String& error, uint32_t holdMs) {
    usbHid.setFeedbackState("ERROR");
    M5Cardputer.Display.clear();
    drawBatteryStatus();
    M5Cardputer.Display.setCursor(0, MantisUI::CONTENT_TOP);
    M5Cardputer.Display.setTextColor(RED);
    M5Cardputer.Display.println("ERROR");
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println(clippedText(error, 38));
    drawFeedbackDot();
    delay(holdMs);
    usbHid.setFeedbackState("READY");
}

void redrawCurrentScreen() {
    switch (currentMode) {
        case MODE_HOME: showHomeMenu(); break;
        case MODE_PAYLOAD_BROWSER: showPayloadBrowser(); break;
        case MODE_ARMED_PAYLOAD: showArmedPayloadScreen(); break;
        case MODE_OTHER: showOtherMenu(); break;
        case MODE_USB_DRIVE: showUsbDriveScreen(); break;
        case MODE_STATUS: showStatusScreen(); break;
        case MODE_SETTINGS: showSettingsScreen(); break;
        default: showHomeMenu(); break;
    }
}

String formatBytes(uint64_t bytes) {
    if (bytes < 1024ULL) return String(static_cast<unsigned long>(bytes)) + " B";
    if (bytes < 1024ULL * 1024ULL) return String(static_cast<unsigned long>(bytes / 1024ULL)) + " KiB";
    if (bytes < 1024ULL * 1024ULL * 1024ULL) return String(static_cast<unsigned long>(bytes / (1024ULL * 1024ULL))) + " MiB";
    const uint32_t hundredths = static_cast<uint32_t>((bytes * 100ULL) / (1024ULL * 1024ULL * 1024ULL));
    return String(hundredths / 100) + "." + String(hundredths % 100) + " GiB";
}

String clippedText(const String& value, size_t maxChars) {
    if (value.length() <= maxChars) return value;
    if (maxChars <= 3) return value.substring(0, maxChars);
    return value.substring(0, maxChars - 3) + "...";
}

String displayPayloadName(const String& name) {
    String result(name);
    String lower(result);
    lower.toLowerCase();
    if (lower.endsWith(".duck")) result = result.substring(0, result.length() - 5);
    else if (lower.endsWith(".txt")) result = result.substring(0, result.length() - 4);
    else if (lower.endsWith(".ds")) result = result.substring(0, result.length() - 3);
    return result;
}

String browserPathLabel() {
    String path = payloadManager.getRelativePath();
    path.toUpperCase();
    return path.length() > 0 ? path : "/";
}

bool uiUsbConnected() {
    return lastUsbConnectedState;
}

uint16_t feedbackColor() {

    if (!sdReady) return RED;
    if (isExecuting) return BLUE;
    return uiUsbConnected() ? GREEN : YELLOW;
}

void drawFeedbackDot() {
    M5Cardputer.Display.fillCircle(234, 7, 3, feedbackColor());
}

void drawBatteryStatus() {
    mantisUi.drawTopBar(FW_VERSION, feedbackColor());
}
