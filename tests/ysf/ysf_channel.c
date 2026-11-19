/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host-only channel model for the YSF modem simulation (docs/ysf-c4fm.md).
 *
 * The transmitter's 8 kHz modulation samples (or a 48 kHz deviation signal)
 * frequency-modulate a complex carrier at 48 kHz with the deviation scaled by
 * dev_scale and a carrier offset; complex white noise sets the carrier-to-noise
 * ratio in a 12.5 kHz bandwidth; a 12.5 kHz channel filter, an ideal
 * discriminator, optional de-emphasis and a 3.4 kHz audio low-pass follow, then decimation to 8 kHz
 * and the chip's audio chain: a first-order high-pass, gain and clipping to
 * int16. Optional first-order high-pass on the transmit side too, and noise
 * alone for a while before the carrier appears (the receiver idling). */
#include "ysf_channel.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { UP = 6, FS48 = 48000 };

static uint64_t state;
static double uniform(void)
{
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return ((state >> 11) + 0.5) / 9007199254740992.0;
}
static double gauss(void) { return sqrt(-2 * log(uniform())) * cos(2 * M_PI * uniform()); }

/* windowed-sinc low-pass, cutoff in Hz at 48 kHz, odd length */
static void lowpass(double *h, int n, double cutoff)
{
    double fc = cutoff / FS48, sum = 0;
    for (int i = 0; i < n; i++) {
        double m = i - (n - 1) / 2.0;
        double s = m == 0 ? 2 * fc : sin(2 * M_PI * fc * m) / (M_PI * m);
        double w = 0.42 - 0.5 * cos(2 * M_PI * i / (n - 1)) + 0.08 * cos(4 * M_PI * i / (n - 1));
        h[i] = s * w;
        sum += h[i];
    }
    for (int i = 0; i < n; i++) h[i] /= sum;
}

static void fir(const double *x, size_t n, const double *h, int taps, double *y)
{
    int half = taps / 2;
    for (size_t i = 0; i < n; i++) {
        double acc = 0;
        for (int k = 0; k < taps; k++) {
            long j = (long)i + half - k;
            if (j >= 0 && (size_t)j < n) acc += h[k] * x[j];
        }
        y[i] = acc;
    }
}

static void highpass(double *x, size_t n, double fc, double fs)
{
    if (fc <= 0) return;
    double a = 1.0 / (1.0 + 2 * M_PI * fc / fs), prev_x = 0, prev_y = 0;
    for (size_t i = 0; i < n; i++) {
        double y = a * (prev_y + x[i] - prev_x);
        prev_x = x[i];
        prev_y = y;
        x[i] = y;
    }
}

size_t ysf_channel_run48(const ysf_channel_cfg *c, const double *dev48, size_t n48, int16_t *out8)
{
    state = c->seed ? c->seed : 0x9E3779B97F4A7C15ull;
    /* lead: noise alone before the carrier appears (whole 8 kHz samples) */
    size_t lead = (size_t)(c->lead_s * 8000) * UP, sig = n48;
    n48 += lead;
    double *re = malloc(n48 * sizeof *re), *im = malloc(n48 * sizeof *im);
    double *fre = malloc(n48 * sizeof *fre), *fim = malloc(n48 * sizeof *fim), *disc = malloc(n48 * sizeof *disc);
    double *aud = malloc(n48 * sizeof *aud);
    double h_if[95], h_aud[191];
    lowpass(h_if, 95, 6250);
    lowpass(h_aud, 191, 3400);
    double phase = 0;
    double sigma = sqrt((double)FS48 / 12500.0 * pow(10, -c->cnr_db / 10.0) / 2.0);
    (void)sig;
    for (size_t i = 0; i < n48; i++) {
        double on = i >= lead;
        phase += 2 * M_PI * ((on ? dev48[i - lead] : 0) * c->dev_scale + c->offset_hz) / FS48;
        /* no carrier and no noise would leave the discriminator undefined: noise at -10 dB C/N floor */
        double s = c->cnr_db < 200 ? sigma : on ? 0 : 0.3;
        re[i] = on * cos(phase) + (s ? s * gauss() : 0);
        im[i] = on * sin(phase) + (s ? s * gauss() : 0);
    }
    fir(re, n48, h_if, 95, fre);
    fir(im, n48, h_if, 95, fim);
    disc[0] = 0;
    for (size_t i = 1; i < n48; i++) {
        double a = fre[i] * fre[i - 1] + fim[i] * fim[i - 1];
        double b = fim[i] * fre[i - 1] - fre[i] * fim[i - 1];
        disc[i] = atan2(b, a) * FS48 / (2 * M_PI);
    }
    if (c->rx_deemph_us > 0) {
        /* the receiver's de-emphasis: a one-pole low-pass on the discriminator, before its audio filter */
        double a = exp(-1.0 / (c->rx_deemph_us * 1e-6 * FS48)), y = 0;
        for (size_t i = 0; i < n48; i++) disc[i] = y = a * y + (1 - a) * disc[i];
    }
    fir(disc, n48, h_aud, 191, aud);
    size_t n8 = n48 / UP;
    double *a8 = malloc(n8 * sizeof *a8);
    for (size_t i = 0; i < n8; i++) a8[i] = aud[i * UP];
    highpass(a8, n8, c->rx_hpf_hz, 8000);
    double clip = c->clip > 0 ? c->clip : 32767;
    for (size_t i = 0; i < n8; i++) {
        double v = a8[i] * c->rx_gain;
        if (v > clip) v = clip;
        if (v < -clip) v = -clip;
        out8[i] = (int16_t)lrint(v);
    }
    free(re); free(im); free(fre); free(fim); free(disc); free(aud); free(a8);
    return n8;
}

size_t ysf_channel_tx48(const int16_t *tx8, size_t n8, double tx_gain, double tx_hpf_hz, double *dev48)
{
    /* The chip's FM modulator interpolates its 8 kHz samples: an ideal 3.6 kHz interpolator */
    size_t n48 = n8 * UP;
    double *x = calloc(n48, sizeof *x), *t8 = malloc(n8 * sizeof *t8);
    for (size_t i = 0; i < n8; i++) t8[i] = tx8[i] / tx_gain;
    highpass(t8, n8, tx_hpf_hz, 8000);
    for (size_t i = 0; i < n8; i++) x[i * UP] = t8[i] * UP;
    double h[191];
    lowpass(h, 191, 3600);
    fir(x, n48, h, 191, dev48);
    free(x); free(t8);
    return n48;
}

size_t ysf_channel_run(const ysf_channel_cfg *c, const int16_t *tx8, size_t n8, double tx_gain, int16_t *out8)
{
    double *y = malloc(n8 * UP * sizeof *y);
    size_t n48 = ysf_channel_tx48(tx8, n8, tx_gain, c->tx_hpf_hz, y);
    size_t r = ysf_channel_run48(c, y, n48, out8);
    free(y);
    return r;
}
