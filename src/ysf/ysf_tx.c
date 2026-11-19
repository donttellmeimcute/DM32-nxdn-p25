/* SPDX-License-Identifier: GPL-3.0-or-later */
/* YSF transmitter: sequence builder + dibit/sample streamer. See ysf_tx.h
 * for the sequence tables (Yaesu Tables 4-3/4-4, MMDVM YSFPayload.cpp). */
#include "ysf_tx.h"
#include <string.h>

unsigned ysf_tx_total_frames(const ysf_tx_config *cfg)
{
    return 2u + 8u * (cfg->cycles ? cfg->cycles : 1u);
}

/* Voice-block length of one 20 ms block in this mode. */
static unsigned voice_len(uint8_t dt)
{
    switch (dt) {
    case YSF_DT_VD_MODE1: return 9;    /* VCH */
    case YSF_DT_VD_MODE2: return 13;   /* VCH 9 + VeCH 4 */
    case YSF_DT_VOICE_FR: return 18;   /* VCH */
    default: return 0;                 /* Data FR: no voice blocks */
    }
}

/* Where the voice bytes of block b start inside the 90-byte payload. */
static unsigned voice_off(uint8_t dt, unsigned b)
{
    switch (dt) {
    case YSF_DT_VD_MODE1: return 18 * b + 9;
    case YSF_DT_VD_MODE2: return 18 * b + 5;
    case YSF_DT_VOICE_FR: return 18 * b;
    default: return 0;
    }
}

static void csd1(const ysf_tx_config *c, uint8_t out[20])
{
    memcpy(out, c->dest, 10);
    memcpy(out + 10, c->src, 10);
}

static void csd2(const ysf_tx_config *c, uint8_t out[20])
{
    memcpy(out, c->down, 10);
    memcpy(out + 10, c->up, 10);
}

static void csd3(const ysf_tx_config *c, uint8_t out[20])
{
    memcpy(out, c->rem1, 10);
    memcpy(out + 10, c->rem2, 10);
}

/* One 10-byte DN data unit: Dest / Src / Down / Up / Rem1+2 / Rem3+4. */
static void dn_unit(const ysf_tx_config *c, unsigned fn, uint8_t out[10])
{
    switch (fn) {
    case 0: memcpy(out, c->dest, 10); break;
    case 1: memcpy(out, c->src, 10); break;
    case 2: memcpy(out, c->down, 10); break;
    case 3: memcpy(out, c->up, 10); break;
    case 4: memcpy(out, c->rem1, 5); memcpy(out + 5, c->rem2, 5); break;
    default: memcpy(out, c->rem3, 5); memcpy(out + 5, c->rem4, 5); break;
    }
}

static void data_unit(const ysf_tx_config *c, unsigned index, uint8_t out[20])
{
    if (c->data) c->data(c->ctx, index, out);
    else memset(out, 0, 20);
}

bool ysf_tx_frame_build(const ysf_tx_config *cfg, unsigned frame_no, uint8_t frame[YSF_FRAME_BYTES])
{
    unsigned cycles = cfg->cycles ? cfg->cycles : 1u;
    unsigned total = 2u + 8u * cycles;
    if (frame_no >= total) return false;
    switch (cfg->dt) {
    case YSF_DT_VD_MODE1: case YSF_DT_DATA_FR: case YSF_DT_VD_MODE2: case YSF_DT_VOICE_FR:
        break;
    default:
        return false;
    }
    ysf_fich f;
    memset(&f, 0, sizeof f);
    f.cs = cfg->cs;
    f.cm = cfg->cm;
    f.dev = cfg->dev;
    f.mr = cfg->mr;
    f.voip = cfg->voip;
    f.dt = cfg->dt;
    f.sql = cfg->sql;
    f.sc = cfg->dgid;
    /* Header and terminator: FI 0 / 2, FN 0, CSD1 + CSD2, mode-independent. */
    if (frame_no == 0 || frame_no == total - 1) {
        f.fi = frame_no == 0 ? YSF_FI_HEADER : YSF_FI_TERMINATOR;
        f.fn = 0;
        uint8_t d1[20], d2[20];
        csd1(cfg, d1);
        csd2(cfg, d2);
        ysf_frame_build(&f, d1, d2, frame);
        return true;
    }
    /* Communication frames: FN cycles 0..7, data index counts DT units. */
    unsigned fn = (frame_no - 1) % 8;
    f.fi = YSF_FI_COMMUNICATION;
    f.fn = (uint8_t)fn;
    uint8_t dch[20], dch2[20];
    memset(dch, 0, sizeof dch);
    memset(dch2, 0, sizeof dch2);
    const uint8_t *second = NULL;
    switch (cfg->dt) {
    case YSF_DT_VD_MODE1:
        if (fn == 0) csd1(cfg, dch);
        else if (fn == 1) csd2(cfg, dch);
        else if (fn == 2) csd3(cfg, dch);
        else data_unit(cfg, fn - 3, dch);   /* FN3..7 = DT1..DT5 */
        break;
    case YSF_DT_VD_MODE2:
        if (fn < 6) dn_unit(cfg, fn, dch);
        else data_unit(cfg, fn - 6, dch);   /* FN6/7 = DT1/DT2, first 10 bytes */
        break;
    case YSF_DT_DATA_FR:
        if (fn == 0) {
            csd1(cfg, dch);
            csd2(cfg, dch2);
            second = dch2;
        } else if (fn == 1) {
            csd3(cfg, dch);
            data_unit(cfg, 0, dch2);        /* DT1 */
            second = dch2;
        } else {
            data_unit(cfg, 2 * fn - 3, dch);       /* DT2, DT4, ... DT12 */
            data_unit(cfg, 2 * fn - 2, dch2);      /* DT3, DT5, ... DT13 */
            second = dch2;
        }
        break;
    default: /* VW: no DCH */
        break;
    }
    ysf_frame_build(&f, dch, second, frame);
    /* Voice blocks: overwrite the deterministic filler with the caller's
     * bytes (or zeros when there is no callback). Header/terminator have no
     * voice blocks; Data FR has none either. */
    unsigned vlen = voice_len(cfg->dt);
    if (vlen) {
        uint8_t *payload = frame + YSF_SYNC_BYTES + YSF_FICH_BYTES;
        for (unsigned b = 0; b < YSF_BLOCKS; b++) {
            uint8_t *vch = payload + voice_off(cfg->dt, b);
            if (cfg->voice) cfg->voice(cfg->ctx, frame_no, b, vch, vlen);
            else memset(vch, 0, vlen);
        }
    }
    return true;
}

void ysf_tx_init(ysf_tx *t, const ysf_tx_config *cfg)
{
    memset(t, 0, sizeof *t);
    t->cfg = *cfg;
    if (!t->cfg.cycles) t->cfg.cycles = 1;
    /* Narrow halves the deviation (1350/450 Hz instead of 2700/900 Hz):
     * the modulator gain is halved, the FICH Dev bit carries the mode. */
    ysf_mod_init(&t->mod, t->cfg.dev ? t->cfg.gain_q8 / 2 : t->cfg.gain_q8);
    t->frame_no = 0;
    /* Load the header frame's dibits now, so the first sample is the first
     * sync symbol: YSF has no preamble and the PA must already be up. */
    uint8_t frame[YSF_FRAME_BYTES];
    if (ysf_tx_frame_build(&t->cfg, 0, frame)) {
        ysf_bytes_to_dibits(frame, YSF_FRAME_BYTES, t->dibits);
        t->frame_no = 1;
    } else {
        /* Invalid config (unknown mode): an empty, already-done stream. */
        t->frame_no = ysf_tx_total_frames(&t->cfg);
        t->dib_pos = YSF_FRAME_DIBITS;
        t->flushed = true;
    }
}

bool ysf_tx_done(const ysf_tx *t)
{
    return t->frame_no >= ysf_tx_total_frames(&t->cfg) && t->dib_pos >= YSF_FRAME_DIBITS &&
           t->flushed && t->pend_pos >= t->pend_n;
}

static bool load_next(ysf_tx *t)
{
    /* Next frame's dibits (false when the sequence is over). */
    if (t->frame_no >= ysf_tx_total_frames(&t->cfg)) return false;
    uint8_t frame[YSF_FRAME_BYTES];
    if (!ysf_tx_frame_build(&t->cfg, t->frame_no, frame)) return false;
    ysf_bytes_to_dibits(frame, YSF_FRAME_BYTES, t->dibits);
    t->dib_pos = 0;
    t->frame_no++;
    return true;
}

size_t ysf_tx_dibits(ysf_tx *t, uint8_t *out, size_t cap)
{
    size_t w = 0;
    while (w < cap) {
        if (t->dib_pos >= YSF_FRAME_DIBITS && !load_next(t)) break;
        out[w++] = t->dibits[t->dib_pos++];
    }
    return w;
}

size_t ysf_tx_samples(ysf_tx *t, int16_t *out, size_t cap)
{
    size_t w = 0;
    /* Drain samples made on an earlier call first. */
    while (w < cap && t->pend_pos < t->pend_n) out[w++] = t->pend[t->pend_pos++];
    if (t->pend_pos >= t->pend_n) { t->pend_n = 0; t->pend_pos = 0; }
    while (w < cap) {
        uint8_t d;
        if (ysf_tx_dibits(t, &d, 1) == 0) break;
        int16_t s[4];
        size_t n = ysf_mod_dibit(&t->mod, d, s);
        for (size_t i = 0; i < n; i++) {
            if (w < cap) out[w++] = s[i];
            else t->pend[t->pend_n++] = s[i];   /* at most 2 per dibit */
        }
    }
    /* End of the dibits: push the modulator's tail out (the last symbols'
     * RRC tails) into the pending buffer, then drain what fits. */
    if (!t->flushed && t->frame_no >= ysf_tx_total_frames(&t->cfg) &&
        t->dib_pos >= YSF_FRAME_DIBITS) {
        int16_t tail[16];
        size_t n = ysf_mod_flush(&t->mod, tail);
        for (size_t i = 0; i < n; i++) t->pend[t->pend_n++] = tail[i];
        t->flushed = true;
    }
    while (w < cap && t->pend_pos < t->pend_n) out[w++] = t->pend[t->pend_pos++];
    if (t->pend_pos >= t->pend_n) { t->pend_n = 0; t->pend_pos = 0; }
    return w;
}
