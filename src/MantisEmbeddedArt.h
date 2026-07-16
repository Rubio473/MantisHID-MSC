#pragma once
#include <Arduino.h>

struct EmbeddedImage {
    uint16_t width;
    uint16_t height;
    const uint16_t* data;
};

extern const EmbeddedImage kArtPayload;
extern const EmbeddedImage kArtSettings;
extern const EmbeddedImage kArtUtilities;
extern const EmbeddedImage kArtUsb;
extern const EmbeddedImage kArtBoot;
