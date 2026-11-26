// SPDX-License-Identifier: GPL-3.0-or-later
// Check control metadata in the frames delivered by the modem.
#include "DMRDefines.h"
#include "DMRFullLC.h"
#include "P25Data.h"
#include "P25NID.h"
#include "YSFFICH.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

void Log(unsigned int, const char*, ...) {}

static void check_dmr(std::istream& stream)
{
    CDMRFullLC lc;
    std::array<unsigned char, 34> frame{};
    unsigned headers = 0, voice = 0, terminators = 0;
    while (stream.read(reinterpret_cast<char*>(frame.data()), frame.size())) {
        const unsigned char control = frame[0];
        if ((control & 0x40U) == 0) {
            assert(headers > 0 && terminators == 0);
            ++voice;
            continue;
        }
        const auto type = static_cast<unsigned char>(control & 0x0FU);
        assert(type == DT_VOICE_LC_HEADER || type == DT_TERMINATOR_WITH_LC);
        const std::unique_ptr<CDMRLC> decoded(lc.decode(frame.data() + 1, type));
        assert(decoded && decoded->getFLCO() == FLCO::GROUP);
        assert(decoded->getSrcId() == 9996543 && decoded->getDstId() == 201);
        if (type == DT_VOICE_LC_HEADER) {
            assert(voice == 0);
            ++headers;
        } else {
            assert(voice > 0);
            ++terminators;
        }
    }
    assert(stream.gcount() == 0 && headers > 0 && voice >= 6 && terminators == 1);
}

static void check_p25(std::istream& stream)
{
    CP25NID nid(0x293);
    CP25Data data;
    std::array<unsigned char, 216> frame{};
    unsigned ldu1 = 0, ldu2 = 0;
    while (stream.read(reinterpret_cast<char*>(frame.data()), frame.size())) {
        assert(nid.decode(frame.data()));
        if (nid.getDUID() == 0x05U) {
            assert(data.decodeLDU1(frame.data()));
            assert(data.getSrcId() == 4567 && data.getDstId() == 123);
            ++ldu1;
        } else {
            assert(nid.getDUID() == 0x0AU && data.decodeLDU2(frame.data()));
            ++ldu2;
        }
    }
    assert(stream.gcount() == 0 && ldu1 >= 2 && ldu2 >= 2);
}

static void check_ysf(std::istream& stream, unsigned char dt, bool narrow)
{
    CYSFFICH fich;
    std::array<unsigned char, 120> frame{};
    unsigned voice = 0;
    bool header = false, terminator = false;
    while (stream.read(reinterpret_cast<char*>(frame.data()), frame.size())) {
        assert(!terminator && fich.decode(frame.data()));
        assert(fich.getDT() == dt && fich.getDev() == narrow);
        switch (fich.getFI()) {
        case 0:
            assert(!header);
            header = true;
            break;
        case 1:
            assert(header);
            ++voice;
            break;
        case 2:
            assert(voice >= 10);
            terminator = true;
            break;
        default:
            assert(false);
        }
    }
    assert(stream.gcount() == 0 && terminator);
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string mode(argv[1]);
    std::ifstream frames(argv[2], std::ios::binary);
    assert(frames);
    if (mode == "dmr")
        check_dmr(frames);
    else if (mode == "p25")
        check_p25(frames);
    else if (mode == "vd1")
        check_ysf(frames, 0, true);
    else if (mode == "dn")
        check_ysf(frames, 2, true);
    else if (mode == "vw")
        check_ysf(frames, 3, false);
    else
        return 2;
    std::printf("%s: MMDVMHost accepted the call metadata\n", argv[1]);
}
