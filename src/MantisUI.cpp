#include "MantisUI.h"

#include <M5Cardputer.h>
#include <pgmspace.h>
#include <cstring>

#include "MantisEmbeddedArt.h"

namespace {
constexpr uint16_t UI_WHITE = 0xFFFF;
constexpr uint16_t UI_BLACK = 0x0000;
constexpr uint16_t UI_GRAY  = 0x8410;
constexpr uint16_t UI_CYAN  = 0x07FF;

const char* const kHomeLabels[4] = {
    "Payloads",
    "Settings",
    "Utilities",
    "USB Drive"
};

bool gTopBarPrimed = false;
char gLastVersion[20] = "";
char gLastTime[6] = "";
int32_t gLastBattery = -999;
uint16_t gLastStatus = 0xFFFF;

void readCurrentTime(char out[6]) {
    strcpy(out, "--:--");
    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t dt{};
        if (M5.Rtc.getDateTime(&dt)) {
            snprintf(out, 6, "%02d:%02d", dt.time.hours, dt.time.minutes);
        }
    }
}

int32_t readCurrentBattery() {
    const int32_t battery = M5.Power.getBatteryLevel();
    return (battery >= 0 && battery <= 100) ? battery : -1;
}

void renderEmbeddedImage(const EmbeddedImage& image, int16_t x, int16_t y) {
    auto& d = M5Cardputer.Display;
    static uint16_t line[160];
    if (image.width > static_cast<int>(sizeof(line) / sizeof(line[0]))) return;

    for (uint16_t row = 0; row < image.height; ++row) {
        for (uint16_t col = 0; col < image.width; ++col) {
            line[col] = pgm_read_word(&image.data[row * image.width + col]);
        }
        d.pushImage(x, y + row, image.width, 1, line);
    }
}

void renderCentered(const EmbeddedImage& image, int16_t topY, int16_t bottomY) {
    auto& d = M5Cardputer.Display;
    const int16_t x = (d.width() - image.width) / 2;
    const int16_t y = topY + ((bottomY - topY + 1) - image.height) / 2;
    renderEmbeddedImage(image, x, y);
}
}

void MantisUI::resetTopBarCache() {
    gTopBarPrimed = false;
    gLastVersion[0] = '\0';
    gLastTime[0] = '\0';
    gLastBattery = -999;
    gLastStatus = 0xFFFF;
}

void MantisUI::drawTopBar(const char* versionLabel, uint16_t statusColor) {
    auto& d = M5Cardputer.Display;
    char timeText[6];
    readCurrentTime(timeText);
    const int32_t battery = readCurrentBattery();
    const char* ver = versionLabel ? versionLabel : "MANTIS V1.0";

    d.startWrite();
    d.fillRect(0, 0, d.width(), TOP_BAR_HEIGHT, UI_BLACK);
    d.setTextSize(1);
    d.setTextColor(UI_WHITE, UI_BLACK);

    d.setCursor(2, 3);
    d.print(ver);

    d.fillRect(192, 2, 32, 10, UI_BLACK);
    d.setCursor(193, 3);
    if (battery >= 0) {
        d.printf("%3ld%%", static_cast<long>(battery));
    } else {
        d.print(" --%");
    }

    d.fillRect(228, 1, 10, 12, UI_BLACK);
    d.fillCircle(234, 7, 3, statusColor);
    d.drawFastHLine(0, TOP_BAR_HEIGHT - 1, d.width(), UI_WHITE);
    d.endWrite();

    strncpy(gLastVersion, ver, sizeof(gLastVersion) - 1);
    gLastVersion[sizeof(gLastVersion) - 1] = '\0';
    strcpy(gLastTime, timeText);
    gLastBattery = battery;
    gLastStatus = statusColor;
    gTopBarPrimed = true;
}

void MantisUI::updateTopBar(const char* versionLabel, uint16_t statusColor) {
    char timeText[6];
    readCurrentTime(timeText);
    const int32_t battery = readCurrentBattery();
    const char* ver = versionLabel ? versionLabel : "MANTIS V1.0";

    if (!gTopBarPrimed || strcmp(ver, gLastVersion) != 0) {
        drawTopBar(ver, statusColor);
        return;
    }

    auto& d = M5Cardputer.Display;
    d.startWrite();
    d.setTextSize(1);
    d.setTextColor(UI_WHITE, UI_BLACK);

    (void)timeText;
    if (battery != gLastBattery) {
        d.fillRect(192, 2, 32, 10, UI_BLACK);
        d.setCursor(193, 3);
        if (battery >= 0) {
            d.printf("%3ld%%", static_cast<long>(battery));
        } else {
            d.print(" --%");
        }
        gLastBattery = battery;
    }

    if (statusColor != gLastStatus) {
        d.fillRect(228, 1, 10, 12, UI_BLACK);
        d.fillCircle(234, 7, 3, statusColor);
        gLastStatus = statusColor;
    }

    d.endWrite();
}

void MantisUI::drawFooter(const char* guide) {
    auto& d = M5Cardputer.Display;
    const int16_t y = d.height() - FOOTER_HEIGHT;
    d.startWrite();
    d.fillRect(0, y, d.width(), FOOTER_HEIGHT, UI_BLACK);
    d.drawFastHLine(0, y, d.width(), UI_GRAY);
    d.setTextSize(1);
    d.setTextColor(UI_GRAY, UI_BLACK);
    d.setCursor(2, y + 2);
    d.print(guide ? guide : "ESC-back  ENTER-open");
    d.endWrite();
}

void MantisUI::drawCornerFrame() {
    auto& d = M5Cardputer.Display;
    constexpr int16_t left = 16;
    constexpr int16_t right = 223;
    constexpr int16_t top = 22;
    constexpr int16_t bottom = 100;
    constexpr int16_t arm = 11;

    d.drawFastHLine(left, top, arm, UI_WHITE);
    d.drawFastVLine(left, top, arm, UI_WHITE);
    d.drawFastHLine(right - arm, top, arm, UI_WHITE);
    d.drawFastVLine(right, top, arm, UI_WHITE);
    d.drawFastHLine(left, bottom, arm, UI_WHITE);
    d.drawFastVLine(left, bottom - arm, arm, UI_WHITE);
    d.drawFastHLine(right - arm, bottom, arm, UI_WHITE);
    d.drawFastVLine(right, bottom - arm, arm, UI_WHITE);
}

void MantisUI::drawCenteredLabel(const char* label, int16_t y, uint16_t color) {
    auto& d = M5Cardputer.Display;
    d.setTextSize(1);
    d.setTextColor(color, UI_BLACK);
    const int16_t width = d.textWidth(label);
    d.setCursor((d.width() - width) / 2, y);
    d.print(label);
}

void MantisUI::drawSelectorHints(uint8_t selected) {
    auto& d = M5Cardputer.Display;
    d.fillTriangle(5, 61, 11, 56, 11, 66, UI_GRAY);
    d.fillTriangle(235, 61, 229, 56, 229, 66, UI_GRAY);

    const int16_t totalWidth = 4 * 11 - 3;
    int16_t x = (d.width() - totalWidth) / 2;
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == selected) d.fillRoundRect(x, 114, 8, 3, 1, UI_CYAN);
        else d.drawRoundRect(x, 114, 8, 3, 1, UI_GRAY);
        x += 11;
    }
}

void MantisUI::drawMenuArt(uint8_t selected) {
    switch (selected % 4) {
        case 0: renderCentered(kArtPayload, 24, 92); break;
        case 1: renderCentered(kArtSettings, 24, 92); break;
        case 2: renderCentered(kArtUtilities, 24, 92); break;
        default: renderCentered(kArtUsb, 24, 92); break;
    }
}

void MantisUI::drawSplash(const char* versionLabel, uint16_t statusColor) {
    (void)versionLabel;
    (void)statusColor;
    auto& d = M5Cardputer.Display;
    d.fillScreen(UI_BLACK);

    renderCentered(kArtBoot, 3, d.height() - 4);
}

void MantisUI::drawHome(uint8_t selected, const char* versionLabel, uint16_t statusColor) {
    auto& d = M5Cardputer.Display;
    selected %= 4;
    d.fillScreen(UI_BLACK);
    drawTopBar(versionLabel, statusColor);
    drawCornerFrame();
    drawMenuArt(selected);
    drawCenteredLabel(kHomeLabels[selected], 103, UI_WHITE);
    drawSelectorHints(selected);
    drawFooter();
}

void MantisUI::drawSectionHeader(const char* title, const char* versionLabel, uint16_t statusColor) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(UI_BLACK);
    drawTopBar(versionLabel, statusColor);
    d.setTextSize(1);
    d.setTextColor(UI_CYAN, UI_BLACK);
    d.setCursor(2, CONTENT_TOP);
    d.println(title ? title : "MANTIS");
    d.drawFastHLine(0, CONTENT_TOP + 10, d.width(), UI_GRAY);
}
