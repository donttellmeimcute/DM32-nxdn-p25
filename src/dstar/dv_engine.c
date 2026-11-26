// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "dv_engine.h"

#include <string.h>
#include "hal_radio.h"

const uint8_t dstar_silence[DSTAR_VOICE_BYTES] = { 0x9e, 0x8d, 0x32, 0x88, 0x26, 0x1a, 0x3f, 0x61, 0xe8 };

// Dibits 01/11 encode positive/negative deviation. The receive correlator can acquire either polarity.
const air_mode_t dstar_air_mode = {
	.name = "D-STAR",
	.profile = {
		.sym_rate = 4800, .levels = 2, .shape = AIR_SHAPE_GAUSS, .shape_param = 50,
		// 6.25 kHz DV channel raster; bw is the supported board RF filter, not the raster.
		.dev_outer_hz = 1200, .timing = AIR_TIMING_CONTINUOUS, .bw = RADIO_BW_12K5,
		.sync_every_syms = DSTAR_FRAME_BITS * DSTAR_SUPERFRAME_FRAMES,
		.idle = DSTAR_IDLE_DIBITS, .nsyncs = 4,
		.sync = {
			{ .id = 0, .nsyms = 24, .max_errs = 1, .flags = AIR_SYNC_START_ONLY,
			  .syms = {0xdd, 0xdd, 0xd5, 0xd7, 0xdd, 0xff} },
			{ .id = 1, .nsyms = 24, .max_errs = 1, .flags = AIR_SYNC_START_ONLY,
			  .syms = {0x77, 0x77, 0x7f, 0x7d, 0x77, 0x55} },
			{ .id = 2, .nsyms = 24, .max_errs = 1,
			  .syms = {0x77, 0x77, 0x75, 0xdf, 0xd7, 0x7f} },
			{ .id = 3, .nsyms = 24, .max_errs = 1,
			  .syms = {0xdd, 0xdd, 0xdf, 0x75, 0x7d, 0xd5} },
		},
	},
};

static void symbols(dstar_engine_t *e, const uint8_t *bits, uint16_t n)
{
	memset(e->symbols, 0, (n + 3u) / 4u);
	for (unsigned i = 0; i < n; i++) {
		unsigned b = (bits[i / 8] >> (i % 8)) & 1u;
		e->symbols[i / 4] |= (b ? 1u : 3u) << (6u - 2u * (i % 4u));
	}
	e->pos = 0;
	e->len = n;
}

static void receive(const air_chunk_t *chunk, void *ctx)
{
	dstar_engine_t *e = ctx;
	if (e->session.tx != AIR_TX_IDLE)
		return;
	if (chunk->flags & (AIR_CHUNK_GAP | AIR_CHUNK_NO_SIGNAL))
		dstar_stream_reset(&e->receive);
	if (chunk->flags & AIR_CHUNK_NO_SIGNAL)
		return;
	for (unsigned i = 0; i < chunk->nsyms; i++) {
		unsigned sym = (chunk->hard[i / 4] >> (6u - 2u * (i % 4u))) & 3u;
		dstar_stream_bit(&e->receive, !(sym & 2u));
	}
}

static void reset_after_tx(void *ctx)
{
	dstar_engine_t *e = ctx;
	dstar_stream_reset(&e->receive);
}

static void fill(void *ctx)
{
	dstar_engine_t *e = ctx;
	for (;;) {
		uint16_t room = air_session_tx_space(&e->session);
		if (room == 0)
			return;
		if (e->pos == e->len) {
			if (e->tail) {
				air_session_tx_finish(&e->session);
				return;
			}
			uint8_t voice[DSTAR_VOICE_BYTES];
			uint8_t frame[DSTAR_FRAME_BYTES];
			bool have_voice = e->hooks.tx_voice && e->hooks.tx_voice(voice, e->ctx);
			if (!have_voice && __atomic_load_n(&e->stopping, __ATOMIC_ACQUIRE)) {
				e->tail = true;
				symbols(e, dstar_end, DSTAR_END_BITS);
			} else {
				static const uint8_t idle[3] = {0x66, 0x66, 0x66};
				if (!have_voice)
					memcpy(voice, dstar_silence, sizeof(voice));
				dstar_frame_encode(frame, voice, idle, e->seq);
				e->seq = (e->seq + 1u) % DSTAR_SUPERFRAME_FRAMES;
				symbols(e, frame, DSTAR_FRAME_BITS);
			}
		}
		uint16_t left = e->len - e->pos;
		uint16_t take = room < left ? room : left;
		if (!air_session_tx(&e->session, e->symbols, e->pos, take))
			return;
		e->pos += take;
	}
}

bool dstar_engine_open(dstar_engine_t *e, const hal_air_t *air, const dstar_engine_hooks_t *hooks, void *ctx)
{
	static const air_session_ops_t ops = { .tx_fill = fill, .tx_done = reset_after_tx, .rx_chunk = receive };
	memset(e, 0, sizeof(*e));
	e->hooks = *hooks;
	e->ctx = ctx;
	dstar_stream_init(&e->receive, &hooks->rx, ctx);
	return air_session_open(&e->session, air, &dstar_air_mode, NULL, NULL, NULL, 0, &ops, e);
}

void dstar_engine_close(dstar_engine_t *e)
{
	air_session_close(&e->session);
	dstar_stream_reset(&e->receive);
}

bool dstar_engine_prime(dstar_engine_t *e, const dstar_header_t *header)
{
	if (air_session_tx_state(&e->session) != AIR_TX_IDLE || !(e->session.fit & AIR_FIT_TX))
		return false;
	uint8_t bits[DSTAR_OPEN_BYTES];
	dstar_open_bits(header, bits);
	symbols(e, bits, DSTAR_OPEN_BITS);
	e->stopping = e->tail = false;
	e->seq = 0;
	return air_session_tx_prime(&e->session);
}

void dstar_engine_stop(dstar_engine_t *e)
{
	__atomic_store_n(&e->stopping, true, __ATOMIC_RELEASE);
}
