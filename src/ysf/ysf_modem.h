/* SPDX-License-Identifier: GPL-3.0-or-later */
/* YSF C4FM modem in software at 8 kHz: 4800 symbols/s, +-2700 / +-900 Hz wide
 * (+-1350 / +-450 Hz narrow, selected by the FICH Dev bit).
 *
 * Mirror of src/nxdn/nxdn_modem.h at the YSF rate: the radio's FM paths carry
 * 8 kHz samples (the receiver's discriminator audio and generated modulation).
 * 8000/4800 = 5/3 samples per symbol, so both directions work with fractional
 * timing:
 *
 *   TX  dibits -> levels -> root-raised-cosine (0.2) x sin(x)/x pulse, 5-phase
 *       polyphase -> int16 samples, `gain` LSB per 1/256 Hz [-> inverse of the
 *       transmit audio path's high-pass]
 *   RX  int16 samples [-> inverses of the receive audio path's de-emphasis and
 *       high-pass] ->
 *       root-raised cosine x x/sin(x) evaluated at interpolated
 *       instants -> Gardner timing loop on zero crossings -> DC (frequency offset) and level
 *       tracking -> hard dibits
 *
 * Integer arithmetic only (the CK803S has no FPU). Tables from
 * tools/gen_ysf_tables.py. No allocation, no RTOS, no hardware.
 *
 * The narrow deviation is a frame-layer concern (the FICH Dev bit): the
 * modulator always produces the wide levels; a narrow transmitter scales the
 * gain by half, and the demodulator tracks the level, so it receives either. */
#ifndef YSF_MODEM_H
#define YSF_MODEM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { YSF_FS = 8000, YSF_RS = 4800, YSF_OUTER_HZ = 2700, YSF_INNER_HZ = 900 };

/* Undoing a first-order high-pass in the radio's audio path (its corner is
 * measured on the radio): x[n] = leak * x[n-1] + y[n] / a - y[n-1], with
 * a = 1 / (1 + 2 pi fc / fs). The leak (1 - 2^-leak_shift per sample, a
 * corner of 8000 / (2 pi 2^leak_shift) Hz) keeps an offset from integrating
 * and bounds the gain below it to fc / leak corner. */
typedef struct {
    int32_t inv_a_q15;     /* 0 = off */
    int32_t x_q4, y_prev;
    uint8_t leak_shift;
} ysf_hpf_inverse;
/* Receive: leak_shift YSF_RX_LEAK. Transmit: YSF_TX_LEAK, since what the
 * transmitter adds below the leak corner moves the carrier (the gain there
 * must stay small enough not to clip or leave the channel). */
enum { YSF_RX_LEAK = 10, YSF_TX_LEAK = 7 };
void ysf_hpf_inverse_init(ysf_hpf_inverse *h, uint16_t corner_hz, uint8_t leak_shift);
int16_t ysf_hpf_inverse_step(ysf_hpf_inverse *h, int16_t y);

/* Undoing a receiver's de-emphasis (a one-pole low-pass of time constant tau,
 * 750 us in FM voice): x[n] = (y[n] - a y[n-1]) / (1 - a), with the
 * bilinear-matched pole a = (2 tau fs - 1) / (2 tau fs + 1). It lifts the
 * band above the corner again; what it lifts beyond the symbol band, the
 * matched filter removes. */
typedef struct {
    int32_t a_q15;         /* 0 = off */
    int32_t inv_q12;       /* 1 / (1 - a), Q12 */
    int32_t prev;
} ysf_deemph_inverse;
void ysf_deemph_inverse_init(ysf_deemph_inverse *e, uint16_t tau_us);
int16_t ysf_deemph_inverse_step(ysf_deemph_inverse *e, int16_t y);

typedef struct {
    int8_t sym[16];        /* the last symbols, ring indexed by count */
    uint32_t symbols;      /* symbols pushed */
    uint32_t samples;      /* samples produced */
    int32_t gain_q8;       /* output LSB per Hz, Q8 */
    ysf_hpf_inverse pre;   /* pre-compensation of the transmit audio path's high-pass */
} ysf_mod;

/* gain_q8: output units per Hz of deviation x 256 (256 = 1 LSB per Hz) */
void ysf_mod_init(ysf_mod *m, int32_t gain_q8);
/* One dibit in; writes the samples now complete (0, 1 or 2) to out. The
 * pulse looks four symbols ahead: samples lag the symbols by four symbols. */
size_t ysf_mod_dibit(ysf_mod *m, uint8_t dibit, int16_t out[4]);
/* Four zero symbols to push the last samples out */
size_t ysf_mod_flush(ysf_mod *m, int16_t out[16]);

typedef struct {
    int16_t ring[64];      /* input samples, indexed by count & 63 */
    uint32_t count;        /* samples in */
    int64_t t_on;          /* next symbol instant, samples in Q16 */
    int32_t prev_on;       /* matched filter output at the last instant */
    int32_t last4[4];      /* ... at the last four (the acquisition's offset) */
    int32_t dc, outer, amp;  /* level tracking: offset, outer level (3 units), threshold (2 units) */
    uint32_t symbols;
    int32_t last_error;    /* Gardner error of the last symbol (diagnostic) */
    int32_t fit;           /* mean distance of the symbols from their decided levels (signal or noise) */
    bool onset_reacquire;  /* acquire again while the symbols don't fit (the caller sets it while it hunts for sync) */
    uint32_t onsets;
    ysf_hpf_inverse post;  /* compensation of the receive audio path's high-pass */
    ysf_deemph_inverse deemph;  /* ... and of its de-emphasis */
} ysf_demod;

void ysf_demod_init(ysf_demod *d);
/* A new transmission may start (the squelch opened, or the frame layer lost
 * sync): fast offset, level and timing acquisition again. The symbol grid
 * restarts at the current sample, so no symbol of the new transmission is
 * skipped (YSF has no preamble: the first symbols are the frame sync). */
void ysf_demod_reacquire(ysf_demod *d);
/* Samples in; hard dibits out (at most cap). Returns the dibits written. */
size_t ysf_demod_samples(ysf_demod *d, const int16_t *in, size_t n, uint8_t *dibits, size_t cap);
/* The soft value of the last symbol relative to the tracked levels: +-3 / +-1 = +-3*256 / +-1*256 */
int32_t ysf_demod_soft(const ysf_demod *d);

#ifdef __cplusplus
}
#endif
#endif
