// SPDX-License-Identifier: ISC
// Copyright (c) 2026 lyra XE1POC
// First-generation AMBE fixed-point reconstruction from mbelib's ISC description and tables.
#include "ambe2400.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "dsp/fxp.h"
#include "mbe/mbe_dsp.h"
#include "ambe2400_tables.h"

#define VOICE_GAIN 5751 // log2 7 in Q11, model amplitudes to PCM
#define PREDICT 21299   // 0.65 in Q15
#define Q11 2048
#define RX_HEADROOM (Q11 / 2)

static const uint8_t positions[9][9] = {
	{0,1,2,3,4,5,48},
	{38,39,40,41},
	{6,7,8,9,42,43},
	{10,11,12,13,14,15,16,44,45},
	{17,18,19,20,21,46,47},
	{22,23,25,26},
	{27,28,29,30},
	{31,32,33,34},
	{35,36,37},
};
static const uint8_t widths[9] = { 7, 4, 6, 9, 7, 4, 4, 4, 3 };
static const int16_t (*const hoc[4])[4] = { hoc0, hoc1, hoc2, hoc3 };

static void fields(const uint8_t *bits, uint16_t *b)
{
	for (unsigned i = 0; i < 9; i++) {
		b[i] = 0;
		for (unsigned j = 0; j < widths[i]; j++)
			b[i] = (b[i] << 1) | (bits[positions[i][j]] & 1u);
	}
	b[8] <<= 1; // the least significant bit of the last codebook index is not carried
}

static void pack(uint16_t *b, uint8_t *bits)
{
	memset(bits, 0, DSTAR_AMBE_BITS);
	b[8] >>= 1;
	for (unsigned i = 0; i < 9; i++)
		for (unsigned j = 0; j < widths[i]; j++)
			bits[positions[i][j]] = (b[i] >> (widths[i] - 1u - j)) & 1u;
}

static void transform(const int32_t *in, unsigned n, int32_t *out, unsigned count)
{
	uint32_t step = UINT32_C(0x40000000) / n;
	for (unsigned k = 0; k < count; k++) {
		int32_t sum = 0;
		uint32_t phase = step * k, delta = 2u * phase;
		for (unsigned i = 0; i < n; i++, phase += delta)
			sum += fxp_mul_q15(in[i], (int16_t)mbe_cos_i(phase));
		out[k] = sum / (int32_t)n;
	}
}

static int32_t reconstruct(const int32_t *c, unsigned count, unsigned at, unsigned n)
{
	int32_t sum = c[0];
	uint32_t step = (UINT32_C(0x40000000) / n) * (2u * at + 1u);
	uint32_t phase = step;
	for (unsigned k = 1; k < count; k++, phase += step)
		sum += fxp_mul_q15(2 * c[k], (int16_t)mbe_cos_i(phase));
	return sum;
}

static unsigned band(uint32_t pitch, unsigned l)
{
	unsigned i = ((pitch >> 12) * (l + 1u)) >> 16;
	return i < 8 ? i : 7;
}

// Resample the last frame's log magnitudes and remove their mean.
static void predict(const dstar_ambe_history_t *h, unsigned n, int32_t *out)
{
	int32_t sum = 0;
	unsigned rem = 0, old = 0;
	for (unsigned i = 0; i < n; i++) {
		rem += h->l;
		while (rem >= n) {
			rem -= n;
			old++;
		}
		if (h->l == 0) {
			out[i] = 0;
		} else {
			int32_t p = h->logm[old == 0 ? 0 : old - 1];
			int32_t q = h->logm[old >= h->l ? h->l - 1u : old];
			out[i] = ((p + (q - p) * (int32_t)rem / (int32_t)n) * PREDICT) >> 15;
		}
		sum += out[i];
	}
	int32_t avg = sum / (int32_t)n;
	for (unsigned i = 0; i < n; i++)
		out[i] -= avg;
}

static void prba(const uint16_t *b, int32_t out[8])
{
	int32_t g[8] = {0};
	for (unsigned i = 0; i < 3; i++)
		g[i + 1] = prba24[b[3]][i];
	for (unsigned i = 0; i < 4; i++)
		g[i + 4] = prba58[b[4]][i];
	for (unsigned i = 0; i < 8; i++)
		out[i] = reconstruct(g, 8, i, 8);
}

static void dequantize(dstar_ambe_history_t *h, const uint16_t *b, mbe_params_t *p, int32_t *shape)
{
	int32_t low[8];
	unsigned bi = b[0], n = lengths[bi], at = 0;
	int32_t avg = 0;
	int32_t coeff[6];

	predict(h, n, shape);
	prba(b, low);
	for (unsigned block = 0; block < 4; block++) {
		unsigned count = blocks[n][block];
		unsigned need = count < 6 ? count : 6;
		unsigned pair = block * 2;
		coeff[0] = (low[pair] + low[pair + 1]) / 2;
		coeff[1] = ((low[pair] - low[pair + 1]) * 11585) >> 15; // 1 / (2 sqrt(2))
		for (unsigned k = 2; k < need; k++)
			coeff[k] = hoc[block][b[5 + block]][k - 2];
		for (unsigned i = 0; i < count; i++, at++) {
			shape[at] += reconstruct(coeff, need, i, count);
			avg += shape[at];
		}
	}

	int32_t gamma = gain[b[2]] + h->gamma / 2;
	int32_t offset = gamma - mbe_log2_q11(n) / 2 - avg / (int32_t)n;
	h->l = n;
	h->gamma = fxp_sat16(gamma);
	p->L = n;
	p->w0 = w0[bi];
	p->flags = 0;
	for (unsigned i = 0; i < n; i++) {
		int32_t model = shape[i] + offset;
		h->logm[i] = fxp_sat16(model);
		p->v[i] = voicing[b[1]][band(p->w0, i)];
		p->a[i] = fxp_sat16(model + VOICE_GAIN + (p->v[i] ? 0 : noise_gain[bi]));
	}
}

int dstar_ambe_unpack(dstar_ambe_history_t *h, const uint8_t bits[DSTAR_AMBE_BITS], mbe_params_t *p)
{
	uint16_t b[9];
	int32_t shape[MBE_MAXL];
	fields(bits, b);
	if (b[0] >= 120) {
		if (b[0] == 125)
			return DSTAR_VOICE_BAD;
		memset(p, 0, sizeof(*p));
		memset(h, 0, sizeof(*h));
		return DSTAR_VOICE_SILENCE;
	}
	dequantize(h, b, p, shape);
	return DSTAR_VOICE_OK;
}

static mbe_mem_t synth_mem(dstar_ambe_t *c)
{
	return (mbe_mem_t){ &c->synth, &c->previous, &c->current, c->scratch };
}

void dstar_ambe_reset(dstar_ambe_t *c)
{
	memset(&c->history, 0, sizeof(c->history));
	mbe_est_init(&c->estimate);
	mbe_mem_t m = synth_mem(c);
	mbe_synth_init(&m);
}

void dstar_ambe_decode(dstar_ambe_t *c, const uint8_t bits[DSTAR_AMBE_BITS], int16_t pcm[MBE_FRAME_LEN])
{
	mbe_mem_t m = synth_mem(c);
	int status = bits ? dstar_ambe_unpack(&c->history, bits, &c->params) : DSTAR_VOICE_BAD;
	if (status == DSTAR_VOICE_OK)
		for (int i = 0; i < c->params.L; i++)
			c->params.a[i] = fxp_sat16((int32_t)c->params.a[i] - RX_HEADROOM);
	int repeat = mbe_synth_frame(&m, &c->params, status == DSTAR_VOICE_BAD, pcm);
	if (repeat > MBE_REPEAT_MAX)
		memset(&c->history, 0, sizeof(c->history));
}

static unsigned nearest(const int32_t *want, const int16_t *table, unsigned entries,
			unsigned stride, unsigned used, unsigned step)
{
	uint32_t best = UINT32_MAX;
	unsigned index = 0;
	for (unsigned i = 0; i < entries; i += step) {
		uint32_t cost = 0;
		for (unsigned j = 0; j < used; j++) {
			int32_t d = (want[j] - table[i * stride + j]) >> 3;
			cost += (uint32_t)(d * d);
		}
		if (cost < best) {
			best = cost;
			index = i;
		}
	}
	return index;
}

static unsigned pitch_code(int target)
{
	unsigned lo = 0, hi = 119;
	while (lo < hi) {
		unsigned mid = (lo + hi) / 2;
		if (period4[mid] < target)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo > 0 && target - period4[lo - 1] <= (int)period4[lo] - target ? lo - 1 : lo;
}

void dstar_ambe_encode(dstar_ambe_t *c, const int16_t pcm[MBE_FRAME_LEN], uint8_t bits[DSTAR_AMBE_BITS])
{
	uint16_t b[9] = {0};
	int32_t residual[MBE_MAXL], r[8], g[8], dct[6];
	mbe_params_t *p = &c->params;
	mbe_est_t e;
	mbe_est_frame(&c->estimate, pcm, c->scratch, &e);
	if (e.zero) {
		b[0] = 124;
		pack(b, bits);
		memset(&c->history, 0, sizeof(c->history));
		return;
	}

	bool pitched = e.period4 != 0 && e.periodic >= -2700;
	b[0] = pitched ? pitch_code(e.period4) : 80;
	if (pitched && e.period4 >= 144) {
		unsigned octave = pitch_code(e.period4 / 2);
		if (mbe_est_mismatch(c->scratch, w0[octave]) + 500 < mbe_est_mismatch(c->scratch, w0[b[0]]))
			b[0] = octave;
	}
	unsigned n = lengths[b[0]];
	p->L = n;
	p->w0 = w0[b[0]];
	unsigned harmonics_per_band = (UINT32_C(0x20000000) + p->w0 / 2u) / p->w0;
	mbe_est_params(&c->estimate, c->scratch, &e, p, harmonics_per_band, 4);
	if (pitched) {
		unsigned best = UINT_MAX;
		for (unsigned v = 0; v < 16; v++) {
			unsigned miss = 0;
			for (unsigned i = 0; i < n; i++)
				miss += (p->v[i] != voicing[v][band(p->w0, i)]) * (i < n / 2 ? 2u : 1u);
			if (miss < best) {
				best = miss;
				b[1] = v;
			}
		}
	}

	predict(&c->history, n, residual);
	int32_t sum = 0;
	for (unsigned i = 0; i < n; i++) {
		int32_t a = p->a[i] == MBE_A_SILENT ? -8 * Q11 : p->a[i] - VOICE_GAIN;
		if (!voicing[b[1]][band(p->w0, i)])
			a -= noise_gain[b[0]];
		residual[i] = fxp_clamp(a, -8 * Q11, 14 * Q11) - residual[i];
		sum += residual[i];
	}
	int32_t mean = sum / (int32_t)n;
	int32_t delta = mean + mbe_log2_q11(n) / 2 - c->history.gamma / 2;
	b[2] = nearest(&delta, gain, 64, 1, 1, 1);
	for (unsigned i = 0; i < n; i++)
		residual[i] -= mean;

	unsigned at = 0;
	for (unsigned block = 0; block < 4; block++) {
		unsigned count = blocks[n][block], need = count < 6 ? count : 6;
		transform(&residual[at], count, dct, need);
		r[block * 2] = dct[0] + fxp_mulshr(dct[1], 46341, 15);
		r[block * 2 + 1] = dct[0] - fxp_mulshr(dct[1], 46341, 15);
		b[5 + block] = nearest(dct + 2, hoc[block][0], 16, 4, need > 2 ? need - 2 : 0, block == 3 ? 2 : 1);
		at += count;
	}
	transform(r, 8, g, 8);
	b[3] = nearest(g + 1, prba24[0], 512, 3, 3, 1);
	b[4] = nearest(g + 4, prba58[0], 128, 4, 4, 1);
	dequantize(&c->history, b, p, residual);
	pack(b, bits);
}
