// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the MMDVMHost oracle (tests/ysf/test_ysf_mmdvm.cc): the YSF
// classes use CUtils::countBits / bitsToByteBE and the Log() function. The
// CUtils functions are copied from MMDVMHost's Utils.cpp; Log is a no-op
// (the oracle never logs).
#include "Utils.h"
#include "Log.h"
#include <cstdarg>
#include <cassert>

unsigned int CUtils::countBits(unsigned int v)
{
    unsigned int count = 0U;
    while (v != 0U) {
        v &= v - 1U;
        count++;
    }
    return count;
}

void CUtils::bitsToByteBE(const bool* bits, unsigned char& byte)
{
    assert(bits != nullptr);

    byte  = bits[0U] ? 0x80U : 0x00U;
    byte |= bits[1U] ? 0x40U : 0x00U;
    byte |= bits[2U] ? 0x20U : 0x00U;
    byte |= bits[3U] ? 0x10U : 0x00U;
    byte |= bits[4U] ? 0x08U : 0x00U;
    byte |= bits[5U] ? 0x04U : 0x00U;
    byte |= bits[6U] ? 0x02U : 0x00U;
    byte |= bits[7U] ? 0x01U : 0x00U;
}

void Log(unsigned int /*level*/, const char* /*fmt*/, ...)
{
}

void LogInitialise(unsigned int /*displayLevel*/, unsigned int /*mqttLevel*/)
{
}

void LogFinalise()
{
}
