#ifndef MANTIS_PAYLOAD_SAFETY_H
#define MANTIS_PAYLOAD_SAFETY_H

#include <Arduino.h>

class PayloadSafety {
public:
    static bool validate(const String& script, String& reason, uint32_t initialDefaultDelayMs = 20);

private:
    static bool allowedCommand(const String& command);
    static bool parseUnsigned(const String& value, uint32_t& result);
};

#endif
