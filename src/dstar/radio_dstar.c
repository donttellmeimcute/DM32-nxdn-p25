// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "config.h"
#if CONFIG_DSTAR
#include <string.h>
#include "board.h"
#include "codeplug/codeplug.h"
#include "radio/radio.h"
#include "radio/mic_gain.h"
#include "dsp/fxp.h"
#include "radio/digital_rx_trim.h"
#include "radio_dstar.h"
#include "dstar_codeplug.h"
#include "ambe_wire.h"
#include "dv_engine.h"
#include "tx_gain.h"

#define QUEUE 8u
#define PCM_PERIOD_US (AUDIO_BLOCK_MS * 1000u)
#define MAX_CODEC_DEBT_US (2u * PCM_PERIOD_US)
#define STACK_BYTES 8192u
#define LOAD(field) __atomic_load_n(&(field), __ATOMIC_ACQUIRE)
#define STORE(field, value) __atomic_store_n(&(field), (value), __ATOMIC_RELEASE)

// The modem task receives and transmits air frames. The audio task decodes
// and encodes PCM. Each queue has one producer and one consumer.
typedef struct {
	uint8_t payload[QUEUE][DSTAR_VOICE_BYTES];
	uint8_t generation[QUEUE];
	uint8_t head, tail;
} voice_queue_t;

typedef struct {
	dstar_engine_t engine;
	dstar_ambe_t codec;
	voice_queue_t rx, tx;
	uint32_t codec_debt_us;
	uint8_t call, ended, tx_gen, codec_gen;
	uint8_t lead;
	bool speaker, tx_slow, keyed;
	bool streaming, sending, codec_slow;
	uint8_t source[8];
	dstar_header_t header;
	bool route_ok;
} dstar_radio_t;

static const hal_audio_t *audio;
static dstar_radio_t *radio;
#ifdef HAL_VOCODER_WINDOW
static void *stack_top;
#else
static dstar_radio_t own;
#endif

static bool put(voice_queue_t *q, uint8_t gen, const uint8_t *frame)
{
	uint8_t h = LOAD(q->head);
	if ((uint8_t)(h - LOAD(q->tail)) >= QUEUE)
		return false;
	unsigned i = h % QUEUE;
	q->generation[i] = gen;
	memcpy(q->payload[i], frame, DSTAR_VOICE_BYTES);
	STORE(q->head, (uint8_t)(h + 1u));
	return true;
}

static bool get(voice_queue_t *q, uint8_t gen, uint8_t *frame)
{
	uint8_t t = LOAD(q->tail), h = LOAD(q->head);
	while (t != h) {
		unsigned i = t++ % QUEUE;
		if (q->generation[i] == gen) {
			memcpy(frame, q->payload[i], DSTAR_VOICE_BYTES);
			STORE(q->tail, t);
			return true;
		}
	}
	STORE(q->tail, t);
	return false;
}

static void start(const dstar_header_t *header, void *ctx)
{
	dstar_radio_t *r = ctx;
	for (unsigned i = 0; i < 8; i++) {
		uint8_t ch = header ? header->my[i] : ' ';
		STORE(r->source[i], ch >= ' ' && ch <= '~' ? ch : ' ');
	}
	STORE(r->call, (uint8_t)(LOAD(r->call) + 1u));
}

static void voice(const uint8_t frame[DSTAR_VOICE_BYTES], void *ctx)
{
	dstar_radio_t *r = ctx;
	put(&r->rx, LOAD(r->call), frame);
}

static void end(void *ctx)
{
	dstar_radio_t *r = ctx;
	STORE(r->ended, LOAD(r->call));
}

static bool tx_voice(uint8_t frame[DSTAR_VOICE_BYTES], void *ctx)
{
	dstar_radio_t *r = ctx;
	return get(&r->tx, LOAD(r->tx_gen), frame);
}

static void speaker(dstar_radio_t *r, bool on)
{
	if (LOAD(r->speaker) == on)
		return;
	if (audio->speaker_pcm)
		audio->speaker_pcm(on);
	STORE(r->speaker, on);
}

typedef struct {
	const int16_t *mic;
	int16_t *spk;
	dstar_radio_t *radio;
} audio_block_t;

static void encode_mic(dstar_ambe_t *codec, const int16_t *mic, int16_t *spk, uint8_t *raw)
{
	static const uint16_t q8[MIC_GAIN_STEPS + 1] = { 81, 102, 128, 161, 203, 256, 322, 406, 511, 643, 810 };
	// Keep the original PCM at the default setting and avoid applying a board gain twice.
	uint16_t gain = mic_gain_on_board() ? 256u : q8[radio_mic_gain()];
	gain = dstar_tx_gain_limit_q8(mic, AUDIO_BLOCK_SAMPLES, gain);
	if (gain == 256u) {
		dstar_ambe_encode(codec, mic, raw);
		return;
	}
	// The speaker is silent on TX. Borrow its block; the frame gain cannot clip PCM.
	for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
		spk[i] = fxp_sat16(((int32_t)mic[i] * gain) / 256);
	dstar_ambe_encode(codec, spk, raw);
	memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(*spk));
}

static void codec_block(void *arg)
{
	audio_block_t *b = arg;
	dstar_radio_t *r = b->radio;
	bool sending = LOAD(r->keyed);
	uint8_t gen = sending ? LOAD(r->tx_gen) : LOAD(r->call);
	if (r->sending != sending || r->codec_gen != gen) {
		r->sending = sending;
		r->codec_gen = gen;
		r->codec_debt_us = 0;
		r->codec_slow = false;
		dstar_ambe_reset(&r->codec);
		r->lead = (audio->speaker_lead_ms + AUDIO_BLOCK_MS - 1u) / AUDIO_BLOCK_MS;
	}
	uint8_t raw[DSTAR_AMBE_BITS], frame[DSTAR_VOICE_BYTES];
	if (r->codec_slow) {
		if (!sending)
			get(&r->rx, gen, frame);
		return;
	}
	uint32_t now = board.air->now_us();
	if (sending) {
		if (!b->mic)
			return;
		encode_mic(&r->codec, b->mic, b->spk, raw);
		dstar_ambe_wire_encode(raw, frame);
		put(&r->tx, gen, frame);
	} else {
		if (LOAD(r->rx.head) == LOAD(r->rx.tail) && LOAD(r->ended) == gen)
			return;
		speaker(r, true);
		if (r->lead != 0) {
			r->lead--;
			return;
		}
		bool received = get(&r->rx, gen, frame);
		if (received || LOAD(r->ended) != gen)
			dstar_ambe_decode(&r->codec, received && dstar_ambe_wire_decode(frame, raw) ? raw : NULL, b->spk);
	}
	// An interrupt or a costly frame can run late once. Cut only when the codec falls
	// two blocks behind the 20 ms stream, counting time it recovers on faster blocks.
	uint32_t elapsed = board.air->now_us() - now;
	if (elapsed <= PCM_PERIOD_US) {
		uint32_t spare = PCM_PERIOD_US - elapsed;
		r->codec_debt_us = spare < r->codec_debt_us ? r->codec_debt_us - spare : 0;
	} else if (elapsed - PCM_PERIOD_US < MAX_CODEC_DEBT_US - r->codec_debt_us) {
		r->codec_debt_us += elapsed - PCM_PERIOD_US;
	} else {
		r->codec_slow = true;
		if (sending)
			STORE(r->tx_slow, true);
		else
			speaker(r, false);
	}
}

static void pcm(const int16_t *mic, int16_t *spk, void *ctx)
{
	dstar_radio_t *r = ctx;
	memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(*spk));
	audio_block_t b = { mic, spk, r };
#ifdef HAL_VOCODER_WINDOW
	board.vocoder->call_on_stack(codec_block, &b, stack_top);
#else
	codec_block(&b);
#endif
	digital_rx_trim_apply(spk);
}

static void pause(dstar_radio_t *r)
{
	if (r->streaming) {
		audio->stop();
		r->streaming = false;
	}
	speaker(r, false);
}

static bool callsign(uint8_t dst[8], const char *name)
{
	bool letter = false, digit = false;
	unsigned i = 0;
	memset(dst, ' ', 8);
	while (i < 8 && name[i] && name[i] != ' ') {
		unsigned ch = (unsigned char)name[i];
		if (ch >= 'a' && ch <= 'z')
			ch -= 'a' - 'A';
		if (ch >= 'A' && ch <= 'Z')
			letter = true;
		else if (ch >= '0' && ch <= '9')
			digit = true;
		else
			return false;
		dst[i++] = ch;
	}
	return letter && digit && (name[i] == '\0' || name[i] == ' ');
}

void radio_dstar_init(const hal_audio_t *a)
{
	audio = a;
}

bool radio_dstar_fits(void)
{
	return board.air && audio && board.air->now_us &&
		(air_session_fit(board.air, &dstar_air_mode) & AIR_FIT_RX) != 0;
}

bool radio_dstar_running(void)
{
	return radio != NULL;
}

void radio_dstar_begin(const channel_t *channel, uint16_t number)
{
	static const dstar_engine_hooks_t hooks = { .rx = { start, voice, end }, .tx_voice = tx_voice };
	radio_dstar_end();
	if (!radio_dstar_fits())
		return;
#ifdef HAL_VOCODER_WINDOW
	if (!board.vocoder || !board.vocoder->lend_window || !board.vocoder->reclaim_window ||
	    !board.vocoder->call_on_stack)
		return;
	size_t size = 0;
	void *mem = board.vocoder->lend_window(&size);
	if (!mem || ((uintptr_t)mem & 7u) || size < sizeof(*radio) + STACK_BYTES) {
		board.vocoder->reclaim_window();
		return;
	}
	radio = mem;
	stack_top = (void *)(((uintptr_t)mem + size) & ~(uintptr_t)7u);
#else
	radio = &own;
#endif
	memset(radio, 0, sizeof(*radio));
	radio->route_ok = dstar_channel_route(number, channel, &radio->header);
	if (!dstar_engine_open(&radio->engine, board.air, &hooks, radio))
		radio_dstar_end();
}

void radio_dstar_end(void)
{
	if (!radio)
		return;
	dstar_engine_close(&radio->engine);
	pause(radio);
	radio = NULL;
#ifdef HAL_VOCODER_WINDOW
	board.vocoder->reclaim_window();
#endif
}

bool radio_dstar_poll(radio_dstar_view_t *view)
{
	radio_dstar_view_t v = {0};
	dstar_radio_t *r = radio;
	if (r && air_session_tx_state(&r->engine.session) == AIR_TX_IDLE) {
		v.rx = LOAD(r->call) != LOAD(r->ended);
		bool queued = LOAD(r->rx.head) != LOAD(r->rx.tail);
		if (!r->streaming && (v.rx || queued))
			r->streaming = audio->start(pcm, r);
		if (r->streaming && !v.rx && !queued && (!audio->spk_pending || !audio->spk_pending()))
			pause(r);
		v.played = LOAD(r->speaker);
		for (unsigned i = 0; i < 8; i++)
			v.source[i] = LOAD(r->source[i]);
		for (unsigned i = 8; i != 0 && v.source[i - 1] == ' '; i--)
			v.source[i - 1] = '\0';
	}
	bool changed = memcmp(view, &v, sizeof(v)) != 0;
	*view = v;
	return changed;
}

static bool no_tx(void)
{
	return radio && !(radio->engine.session.fit & AIR_FIT_TX);
}

static uint8_t tx_fault(void)
{
	if (radio && LOAD(radio->tx_slow))
		return TXDENY_CPU;
	return TXDENY_NONE;
}

static bool tx_prepare(void)
{
	dstar_radio_t *r = radio;
	char name[CODEPLUG_NAME_LEN + 1];
	if (!r || !r->route_ok)
		return false;
	codeplug_radio_name(name);
	if (!callsign(r->header.my, name))
		return false;
	pause(r);
	STORE(r->rx.tail, LOAD(r->rx.head));
	STORE(r->tx_gen, (uint8_t)(LOAD(r->tx_gen) + 1u));
	return dstar_engine_prime(&r->engine, &r->header);
}

static bool tx_go(void)
{
	dstar_radio_t *r = radio;
	if (!r || !air_session_tx_go(&r->engine.session))
		return false;
	STORE(r->keyed, true);
	r->streaming = audio->start(pcm, r);
	if (!r->streaming) {
		STORE(r->keyed, false);
		air_session_tx_abort(&r->engine.session);
	}
	return r->streaming;
}

static void tx_stop(void)
{
	if (!radio)
		return;
	pause(radio);
	dstar_engine_stop(&radio->engine);
}

static void tx_abort(void)
{
	if (radio) {
		pause(radio);
		air_session_tx_abort(&radio->engine.session);
	}
}

static uint8_t tx_state(void)
{
	return radio ? (uint8_t)air_session_tx_state(&radio->engine.session) : AIR_TX_IDLE;
}

static void tx_clear(void)
{
	if (!radio)
		return;
	pause(radio);
	STORE(radio->keyed, false);
	STORE(radio->tx_slow, false);
	air_session_tx_clear(&radio->engine.session);
}

const radio_air_ops_t radio_dstar_air = {
	.running = radio_dstar_running, .no_tx = no_tx, .tx_prepare = tx_prepare,
	.tx_go = tx_go, .tx_stop = tx_stop, .tx_abort = tx_abort,
	.tx_state = tx_state, .tx_clear = tx_clear, .tx_fault = tx_fault,
};
#endif
