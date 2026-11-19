/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host-only FM channel model for the YSF modem simulation (docs/ysf-c4fm.md).
 * Mirror of tests/nxdn/nxdn_channel.h at the YSF deviation: the transmitter's
 * 8 kHz modulation samples (or a 48 kHz deviation signal) frequency-modulate a
 * complex carrier at 48 kHz; complex white noise sets the carrier-to-noise
 * ratio in a 12.5 kHz bandwidth; a 12.5 kHz channel filter, an ideal
 * discriminator, optional de-emphasis and a 3.4 kHz audio low-pass follow,
 * then decimation to 8 kHz and the chip's audio chain: a first-order
 * high-pass, gain and clipping to int16. Optional first-order high-pass on
 * the transmit side too, and noise alone for a while before the carrier
 * appears (the receiver idling). */
#ifndef YSF_CHANNEL_H
#define YSF_CHANNEL_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    double cnr_db;        /* carrier to noise in 12.5 kHz; >= 200 = no noise */
    double offset_hz;     /* carrier frequency error */
    double dev_scale;     /* transmitted deviation / nominal */
    double tx_hpf_hz;     /* first-order high-pass in the transmit audio path, 0 = none */
    double rx_hpf_hz;     /* first-order high-pass in the receive audio path, 0 = none */
    double rx_deemph_us;  /* the receiver's de-emphasis time constant, 0 = none (flat discriminator) */
    double rx_gain;       /* receive audio LSB per Hz */
    double clip;          /* receive audio clipping level in LSB, 0 = int16 limits */
    double lead_s;        /* noise alone before the carrier, seconds (output grows by lead_s x 8000) */
    uint64_t seed;
} ysf_channel_cfg;

/* tx8: the modulator's samples (tx_gain LSB per Hz) -> out8 receive audio. Returns samples written (n8 + lead) */
size_t ysf_channel_run(const ysf_channel_cfg *c, const int16_t *tx8, size_t n8, double tx_gain, int16_t *out8);
/* The same from a 48 kHz deviation signal in Hz (another transmitter's waveform) */
size_t ysf_channel_run48(const ysf_channel_cfg *c, const double *dev48, size_t n48, int16_t *out8);
/* The transmitter's 8 kHz samples as the 48 kHz deviation (Hz) the chip modulates */
size_t ysf_channel_tx48(const int16_t *tx8, size_t n8, double tx_gain, double tx_hpf_hz, double *dev48);
#endif
