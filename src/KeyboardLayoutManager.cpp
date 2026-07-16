#include "KeyboardLayoutManager.h"

#include <algorithm>

bool KeyboardLayoutManager::loadFromWholeSd(SDWholeCardStorage& storage) {
    if (!reader.begin(storage)) return false;

    std::vector<WholeSdFileEntry> entries;
    if (!reader.listDirectory("/MantisSD/Layouts", entries, 80)) return false;

    std::vector<MantisExternalLayout> loaded;
    for (const WholeSdFileEntry& entry : entries) {
        if (entry.isDir || entry.size != 256) continue;
        String lower = entry.name;
        lower.toLowerCase();
        if (!lower.endsWith(".kl")) continue;

        MantisExternalLayout layout;
        layout.name = entry.name.substring(0, entry.name.length() - 3);
        if (layout.name.equalsIgnoreCase("LATAM") ||
            layout.name.equalsIgnoreCase("ES") ||
            layout.name.equalsIgnoreCase("US")) {
            continue;
        }

        uint8_t raw[256];
        const String path = "/MantisSD/Layouts/" + entry.name;
        if (!reader.readFileRange(path, 0, raw, sizeof(raw))) continue;

        for (size_t i = 0; i < 128; ++i) {
            layout.map[i] = static_cast<uint16_t>(raw[i * 2]) |
                            (static_cast<uint16_t>(raw[i * 2 + 1]) << 8);
        }
        loaded.push_back(layout);
    }

    std::sort(loaded.begin(), loaded.end(), [](const MantisExternalLayout& a, const MantisExternalLayout& b) {
        String left = a.name;
        String right = b.name;
        left.toLowerCase();
        right.toLowerCase();
        return left < right;
    });

    externalLayouts.swap(loaded);
    Serial.printf("LAYOUT SCAN: %u external layouts loaded\n", static_cast<unsigned>(externalLayouts.size()));
    return true;
}

size_t KeyboardLayoutManager::count() const {
    return 3 + externalLayouts.size();
}

String KeyboardLayoutManager::nameAt(size_t index) const {
    if (index == 0) return "LATAM";
    if (index == 1) return "ES";
    if (index == 2) return "US";
    const size_t externalIndex = index - 3;
    if (externalIndex >= externalLayouts.size()) return "US";
    return externalLayouts[externalIndex].name;
}

int KeyboardLayoutManager::indexOf(const String& name) const {
    if (name.equalsIgnoreCase("LATAM")) return 0;
    if (name.equalsIgnoreCase("ES")) return 1;
    if (name.equalsIgnoreCase("US")) return 2;
    for (size_t i = 0; i < externalLayouts.size(); ++i) {
        if (externalLayouts[i].name.equalsIgnoreCase(name)) return static_cast<int>(i + 3);
    }
    return -1;
}

bool KeyboardLayoutManager::isBuiltIn(const String& name) const {
    return name.equalsIgnoreCase("LATAM") || name.equalsIgnoreCase("ES") || name.equalsIgnoreCase("US");
}

const uint16_t* KeyboardLayoutManager::tableFor(const String& name) const {
    for (const MantisExternalLayout& layout : externalLayouts) {
        if (layout.name.equalsIgnoreCase(name)) return layout.map;
    }
    return nullptr;
}

String KeyboardLayoutManager::normalizedName(const String& name) const {
    const int index = indexOf(name);
    return index >= 0 ? nameAt(static_cast<size_t>(index)) : String("US");
}
