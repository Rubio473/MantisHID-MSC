#ifndef MANTIS_KEYBOARD_LAYOUT_MANAGER_H
#define MANTIS_KEYBOARD_LAYOUT_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "SDWholeCardStorage.h"
#include "WholeSdFat32Reader.h"

struct MantisExternalLayout {
    String name;
    uint16_t map[128];
};

class KeyboardLayoutManager {
public:
    bool loadFromWholeSd(SDWholeCardStorage& storage);
    size_t count() const;
    String nameAt(size_t index) const;
    int indexOf(const String& name) const;
    bool isBuiltIn(const String& name) const;
    const uint16_t* tableFor(const String& name) const;
    String normalizedName(const String& name) const;

private:
    std::vector<MantisExternalLayout> externalLayouts;
    WholeSdFat32Reader reader;
};

#endif
