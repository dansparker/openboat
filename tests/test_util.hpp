#pragma once

#include <cstdio>
#include <string>

// Appends "*hh" to a sentence like "$GPRMC,..." so tests need no hand-made checksums.
inline std::string with_checksum(const std::string& body) {
    unsigned sum = 0;
    for (std::size_t i = 1; i < body.size(); ++i) sum ^= static_cast<unsigned char>(body[i]);
    char buf[4];
    std::snprintf(buf, sizeof buf, "*%02X", sum);
    return body + buf;
}
