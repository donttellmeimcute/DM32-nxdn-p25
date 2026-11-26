// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The upstream RX files include Globals.h for board I/O and serial output.
// Replace those interfaces in the host build, leaving all receiver logic intact.
#define GLOBALS_H
#include <cstdint>

using q15_t = int16_t;
using q31_t = int32_t;
using float32_t = float;

class CSerialPort {
public:
    void writeDMRData(bool, const uint8_t*, uint8_t);
    void writeDMRLost(bool) {}
    void writeDStarHeader(const uint8_t*, uint8_t);
    void writeDStarData(const uint8_t*, uint8_t);
    void writeDStarEOT();
    void writeDStarLost() {}
    void writeYSFData(const uint8_t*, uint8_t);
    void writeYSFLost() {}
    void writeP25Hdr(const uint8_t*, uint8_t);
    void writeP25Ldu(const uint8_t*, uint8_t);
    void writeP25Lost() {}
    void writeNXDNData(const uint8_t*, uint8_t);
    void writeNXDNLost() {}
    void writeM17LinkSetup(const uint8_t*, uint8_t);
    void writeM17Stream(const uint8_t*, uint8_t);
    void writeM17Packet(const uint8_t*, uint8_t) {}
    void writeM17EOT();
    void writeM17Lost() {}
};

class CIO {
public:
    void setDecode(bool) {}
    void setADCDetection(bool) {}
};

extern CSerialPort serial;
extern CIO io;
inline void LogMessage(const char*, ...) {}

#define DEBUG1(...) ((void)0)
#define DEBUG4(...) ((void)0)
#define DEBUG5(...) ((void)0)
