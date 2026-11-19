/* SPDX-License-Identifier: GPL-3.0-or-later */
/* YSF transmitter, host prototype: builds a whole transmission (header ->
 * communication frames FN 0..7 -> terminator) and streams it as dibits or as
 * 8 kHz C4FM samples through ysf_mod. Mirror of the RX frame layer
 * (ysf_frame.h) and the modulator (ysf_modem.h): the TX chain is
 *
 *   ysf_tx_config (callsigns, mode, DG-ID, deviation)
 *     -> ysf_tx_frame_build() (FICH + DCH rotation + voice blocks)
 *     -> ysf_frame_build() (sync + FICH + DCH encode)
 *     -> ysf_tx_dibits() / ysf_tx_samples() (ysf_mod, RRC 0.2, 4800 sym/s)
 *
 * Frame sequence (Yaesu Fig. 4-1; MMDVM YSFControl.cpp / YSFPayload.cpp):
 * one header (FI 0), eight communication frames (FI 1, FN 0..7), one
 * terminator (FI 2). Longer transmissions repeat the communication block
 * (cycles > 1). FICH: FT/BN/BT are 0, as MMDVM leaves them on transmit; DT is
 * the configured mode on every frame; Dev selects Wide (0) / Narrow (1) and
 * halves the modulator gain; SC is the DG-ID.
 *
 * DCH rotation per mode (Yaesu Tables 4-3/4-4; MMDVM processVDMode1Data /
 * processVDMode2Data / processDataFRModeData):
 *   V/D-1 (DT 0): FN0=CSD1 (Dest+Src), FN1=CSD2 (Down+Up), FN2=CSD3
 *     (Rem1+Rem2), FN3..7 = data DT1..DT5 (20 bytes each).
 *   DN (DT 2): FN0=Dest, FN1=Src, FN2=Down, FN3=Up, FN4=Rem1+Rem2 (5 bytes
 *     each), FN5=Rem3+Rem4, FN6/7 = data DT1/DT2 (10 bytes each).
 *   Data FR (DT 1): FN0: DCH-1=CSD1, DCH-2=CSD2; FN1: DCH-1=CSD3,
 *     DCH-2=DT1; FN2..7: DCH-1/DCH-2 = DT2/DT3 .. DT12/DT13 (20 bytes each).
 *   VW (DT 3): no DCH in communication frames. The CC0 sub-header of Yaesu
 *     Fig. 4-22 (5x72 DCH + 72 reserved + 2x144 VCH) is NOT built: no source
 *     states what its DCH carries.
 * The header and the terminator always carry CSD1 + CSD2 (two DCH1s),
 * whatever the mode (MMDVM writeHeader / processHeaderData).
 *
 * Voice: the caller supplies the voice bytes of each 20 ms block through the
 * voice callback (9 bytes V/D-1, 13 bytes DN: VCH 9 + VeCH 4, 18 bytes VW).
 * With no callback the voice blocks are all zero: a deterministic test
 * payload, NOT a valid AMBE silence frame. Real voice (AMBE+2 half-rate,
 * 49 parameter bits -> 72-bit VCH) is a separate problem: the parameter
 * interface of the DVSI blob and the 49->72 channel-FEC mapping still need
 * proof against a real YSF voice capture or vector (docs/ysf-c4fm.md).
 *
 * Integer arithmetic only, no allocation. */
#ifndef YSF_TX_H
#define YSF_TX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ysf_frame.h"
#include "ysf_modem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Data units: the data callback fills one unit (20 bytes; DN uses the first
 * 10). index counts DT units from 0 (DT1): V/D-1 FN3..7 -> 0..4, DN FN6/7 ->
 * 0..1, Data FR FN1 DCH-2 .. FN7 DCH-2 -> 0..12. */
typedef void (*ysf_tx_data_fn)(void *ctx, unsigned index, uint8_t out[20]);
/* Voice blocks: fills one 20 ms block's voice bytes (vch_len: 9 V/D-1,
 * 13 DN, 18 VW). frame_no is the sequence frame number (1..8*cycles,
 * communication frames only). */
typedef void (*ysf_tx_voice_fn)(void *ctx, unsigned frame_no, unsigned block,
                                uint8_t *vch, unsigned vch_len);

typedef struct {
    uint8_t dt;            /* YSF_DT_VD_MODE1 / DATA_FR / VD_MODE2 / VOICE_FR */
    uint8_t dev;           /* 0 = Wide (+-2700/+-900 Hz), 1 = Narrow (+-1350/+-450 Hz) */
    uint8_t cm;            /* YSF_CM_GROUP / RADIO_ID / INDIVIDUAL */
    uint8_t mr;            /* YSF_MR_DIRECT / DOWNLINK_FREE / DOWNLINK_BUSY */
    uint8_t cs;            /* 2 bits, usually 0 */
    uint8_t sql;           /* 0 = DG-ID squelch enabled, 1 = disabled */
    uint8_t voip;          /* 0 = RF, 1 = VoIP */
    uint8_t dgid;          /* FICH SC: 0 = none, 1..126 = codes, 127 = Break Out */
    char dest[YSF_CALLSIGN_LEN];
    char src[YSF_CALLSIGN_LEN];
    char down[YSF_CALLSIGN_LEN];
    char up[YSF_CALLSIGN_LEN];
    char rem1[YSF_CALLSIGN_LEN];
    char rem2[YSF_CALLSIGN_LEN];
    char rem3[YSF_CALLSIGN_LEN];
    char rem4[YSF_CALLSIGN_LEN];
    unsigned cycles;       /* communication-block repeats, >= 1 (1 = 10 frames) */
    int32_t gain_q8;       /* modulator gain, wide (256 = 1 LSB per Hz); halved when dev == 1 */
    ysf_tx_data_fn data;   /* NULL: data units are zero-filled */
    ysf_tx_voice_fn voice; /* NULL: voice blocks are zero-filled (test payload, not AMBE) */
    void *ctx;
} ysf_tx_config;

/* Total frames of a transmission: 2 + 8 * cycles (header + cycles x 8 CC + terminator). */
unsigned ysf_tx_total_frames(const ysf_tx_config *cfg);

/* Build one frame of the sequence: frame_no 0 = header, 1..8*cycles =
 * communication (FN (frame_no-1) % 8), last = terminator. False when
 * frame_no is out of range or the mode is unknown. */
bool ysf_tx_frame_build(const ysf_tx_config *cfg, unsigned frame_no, uint8_t frame[YSF_FRAME_BYTES]);

typedef struct {
    ysf_tx_config cfg;
    ysf_mod mod;
    unsigned frame_no;     /* next frame to build */
    uint8_t dibits[YSF_FRAME_DIBITS];
    unsigned dib_pos;      /* next dibit to emit from dibits[] */
    int16_t pend[16];      /* samples made but not yet handed out */
    unsigned pend_n, pend_pos;
    bool flushed;          /* ysf_mod_flush already emitted */
} ysf_tx;

void ysf_tx_init(ysf_tx *t, const ysf_tx_config *cfg);
/* True when every dibit is out and the modulator tail is flushed. */
bool ysf_tx_done(const ysf_tx *t);
/* Dibites out (at most cap); 0 when the transmission is over. */
size_t ysf_tx_dibits(ysf_tx *t, uint8_t *out, size_t cap);
/* 8 kHz samples out (at most cap); 0 when the transmission is over and the
 * modulator tail is flushed. Pulls dibits through ysf_mod as needed. */
size_t ysf_tx_samples(ysf_tx *t, int16_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
