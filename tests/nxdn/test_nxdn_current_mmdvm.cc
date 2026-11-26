// SPDX-License-Identifier: GPL-3.0-or-later
// Decode captured NXDN frames with MMDVMHost's NXDN decoder.
#include "NXDNDefines.h"
#include "NXDNFACCH1.h"
#include "NXDNLICH.h"
#include "NXDNSACCH.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static constexpr unsigned sync_symbols = 10;
static constexpr unsigned frame_symbols = 192;

static unsigned sync_at(const unsigned char* symbols)
{
	unsigned word = 0;
	for (unsigned i = 0; i < sync_symbols; i++)
		word = (word << 2) | symbols[i];
	return word;
}

static void pack_frame(const unsigned char* symbols, unsigned char* frame)
{
	// NXDN TS 1-A 4.6: reset PN9 at the LICH and invert the sign bit only.
	unsigned pn = 0x0E4;
	for (unsigned i = 0; i < frame_symbols; i++) {
		unsigned dibit = symbols[i];
		if (i >= sync_symbols) {
			dibit ^= (pn & 1u) << 1;
			pn = (pn >> 1) | (((pn ^ (pn >> 4)) & 1u) << 8);
		}
		frame[i / 4] |= dibit << (6 - 2 * (i % 4));
	}
}

int main(int argc, char** argv)
{
	assert(argc == 2);
	std::ifstream stream(argv[1], std::ios::binary);
	assert(stream);
	std::vector<unsigned char> symbols{std::istreambuf_iterator<char>(stream), {}};
	unsigned pos = 0;
	while (pos + frame_symbols <= symbols.size() && sync_at(&symbols[pos]) != 0xCDF59)
		pos++;
	assert(pos >= sync_symbols && pos + frame_symbols <= symbols.size());

	unsigned frames = 0, voice_frames = 0, ran = 0;
	bool released = false;
	for (; pos + frame_symbols <= symbols.size(); pos += frame_symbols, frames++) {
		assert(!released && sync_at(&symbols[pos]) == 0xCDF59);
		unsigned char frame[frame_symbols / 4]{};
		pack_frame(&symbols[pos], frame);
		CNXDNLICH lich;
		assert(lich.decode(frame) && lich.getRFCT() == NXDN_LICH_RFCT_RDCH && lich.getDirection() == 0);
		CNXDNSACCH sacch;
		assert(sacch.decode(frame));
		if (frames == 0)
			ran = sacch.getRAN();
		assert(sacch.getRAN() == ran);

		if (lich.getFCT() == NXDN_LICH_USC_SACCH_SS) {
			assert(frames > 0);
			voice_frames++;
			continue;
		}
		assert(lich.getFCT() == NXDN_LICH_USC_SACCH_NS);
		CNXDNFACCH1 facch;
		assert(facch.decode(frame, NXDN_FSW_LENGTH_BITS + NXDN_LICH_LENGTH_BITS + NXDN_SACCH_LENGTH_BITS));
		unsigned char data[10]{};
		facch.getData(data);
		unsigned expected = frames == 0 ? NXDN_MESSAGE_TYPE_VCALL : NXDN_MESSAGE_TYPE_TX_REL;
		assert((data[0] & 0x3Fu) == expected);
		assert(((data[3] << 8) | data[4]) == 4567 && ((data[5] << 8) | data[6]) == 123);
		released = frames != 0;
	}
	assert(voice_frames > 0 && released);
	std::printf("MMDVMHost decoded NXDN: VCALL 4567 -> 123, RAN %u, %u traffic frames, TX_REL\n",
	            ran, voice_frames);
}
