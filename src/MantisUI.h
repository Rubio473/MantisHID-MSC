#pragma once

#include <Arduino.h>

class MantisUI {
public:
    static constexpr int16_t TOP_BAR_HEIGHT = 16;
    static constexpr int16_t FOOTER_HEIGHT = 11;
    static constexpr int16_t CONTENT_TOP = TOP_BAR_HEIGHT + 2;
    static constexpr int16_t CONTENT_BOTTOM = 135 - FOOTER_HEIGHT - 1;

    void drawTopBar(const char* versionLabel, uint16_t statusColor);
    void updateTopBar(const char* versionLabel, uint16_t statusColor);
    void drawFooter(const char* guide = "ESC-back  ENTER-open");
    void drawHome(uint8_t selected, const char* versionLabel, uint16_t statusColor);
    void drawSplash(const char* versionLabel, uint16_t statusColor);
    void drawSectionHeader(const char* title, const char* versionLabel, uint16_t statusColor);
    void resetTopBarCache();

private:
    void drawCornerFrame();
    void drawSelectorHints(uint8_t selected);
    void drawCenteredLabel(const char* label, int16_t y, uint16_t color = 0xFFFF);
    void drawMenuArt(uint8_t selected);
};
