// SPDX-License-Identifier: GPL-3.0-or-later
// Feed simulated discriminator samples into the MMDVM receive path.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#if defined(TEST_M17)
#include "M17RX.h"
#else
#include "DMRDMORX.h"
#include "DStarRX.h"
#include "NXDNRX.h"
#include "P25RX.h"
#include "YSFRX.h"
#endif

CSerialPort serial;
CIO io;

static void emit(const char* kind, const uint8_t* data, uint8_t size)
{
    std::printf("%s", kind);
    for (unsigned i = 0; i < size; ++i)
        std::printf("%s%02x", i ? "" : " ", data[i]);
    std::puts("");
}

void CSerialPort::writeDMRData(bool, const uint8_t* data, uint8_t size) { emit("dmr", data, size); }
void CSerialPort::writeDStarHeader(const uint8_t* data, uint8_t size) { emit("dstar-header", data, size); }
void CSerialPort::writeDStarData(const uint8_t* data, uint8_t size) { emit("dstar-voice", data, size); }
void CSerialPort::writeDStarEOT() { emit("dstar-end", nullptr, 0); }
void CSerialPort::writeYSFData(const uint8_t* data, uint8_t size) { emit("ysf", data, size); }
void CSerialPort::writeP25Hdr(const uint8_t* data, uint8_t size) { emit("p25-header", data, size); }
void CSerialPort::writeP25Ldu(const uint8_t* data, uint8_t size) { emit("p25-ldu", data, size); }
void CSerialPort::writeNXDNData(const uint8_t* data, uint8_t size) { emit("nxdn", data, size); }
void CSerialPort::writeM17LinkSetup(const uint8_t* data, uint8_t size) { emit("m17-link", data, size); }
void CSerialPort::writeM17Stream(const uint8_t* data, uint8_t size) { emit("m17-voice", data, size); }
void CSerialPort::writeM17EOT() { emit("m17-end", nullptr, 0); }

template<class Receiver>
static bool replay(Receiver& rx, std::istream& dibits, uint8_t samples_per_symbol, int scale)
{
    const int levels[] = {-1, -3, 1, 3};
    uint16_t rssi[10] = {};
    q15_t samples[10] = {};
    // Let the receive ring settle before the captured preamble and after PTT.
    for (unsigned i = 0; i < 48; ++i)
        rx.samples(samples, rssi, samples_per_symbol);
    for (int dibit; (dibit = dibits.get()) != EOF;) {
        // 4 represents the gap between simplex DMR slots.
        if (dibit > 4)
            return false;
        const int level = dibit == 4 ? 0 : levels[dibit] * scale;
        const int centre = samples_per_symbol / 2;
        for (int i = 0; i < samples_per_symbol; ++i)
            samples[i] = level * (20 - std::abs(i - centre)) / 20;
        rx.samples(samples, rssi, samples_per_symbol);
    }
    std::fill_n(samples, samples_per_symbol, 0);
    for (unsigned i = 0; i < 48; ++i)
        rx.samples(samples, rssi, samples_per_symbol);
    return dibits.eof();
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    std::ifstream capture(argv[2], std::ios::binary);
    if (!capture)
        return 2;
    const std::string mode(argv[1]);
#if defined(TEST_M17)
    if (mode == "m17") {
        CM17RX rx;
        return replay(rx, capture, 5, 500) ? 0 : 1;
    }
#else
    if (mode == "dmr") {
        CDMRDMORX rx;
        rx.setColorCode(2);
        return replay(rx, capture, 5, 500) ? 0 : 1;
    }
    if (mode == "p25") {
        CP25RX rx;
        return replay(rx, capture, 5, 500) ? 0 : 1;
    }
    if (mode == "dn" || mode == "vd1" || mode == "vw") {
        CYSFRX rx;
        return replay(rx, capture, 5, mode == "vw" ? 500 : 250) ? 0 : 1;
    }
    if (mode == "nxdn48" || mode == "nxdn96") {
        CNXDNRX rx;
        return replay(rx, capture, 10, 500) ? 0 : 1;
    }
    if (mode == "dstar") {
        CDStarRX rx;
        return replay(rx, capture, 5, 500) ? 0 : 1;
    }
#endif
    return 2;
}
