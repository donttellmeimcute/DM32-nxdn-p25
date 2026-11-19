// SPDX-License-Identifier: GPL-3.0-or-later
// YSF frame layer against MMDVMHost's own classes (YSFFICH, YSFPayload,
// Golay24128): our encoder's output decoded by MMDVM, and MMDVM's encoder
// output decoded by us. This is the only external anchor available: there
// are no public YSF test vectors.
//
// Known MMDVM limits, handled here rather than worked around:
// - MMDVM reads MR as 2 bits (getMR masks & 0x03) although it writes 3, so
//   the cross-check only uses MR 0..2; MR 3..7 round-trips in our self-test.
// - MMDVM has no setters for CM, DT, CS or SQL and no getters for CS or SQL,
//   so MMDVM-encoded FICH frames always carry CM 0, DT 0, CS 0, SQL 0.
// - MMDVM has no writer for a DN (DCH2) data block; the DN reverse direction
//   goes through its re-encode path (processVDMode2Data rewrites the DCH
//   clean after correcting it).
#include "ysf_frame.h"
#include "YSFFICH.h"
#include "YSFPayload.h"
#include "Golay24128.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t rnd_state = 0x12345678u;
static uint32_t rnd_next(void)
{
    rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static const char dest_cs[11] = "CQ        ";
static const char src_cs[11] = "XE1ARS    ";
static const char down_cs[11] = "XE1ARS-R  ";
static const char up_cs[11] = "XE1ARS    ";

static void golay_checks(void)
{
    /* Encode: every 12-bit value must match MMDVM's table. */
    for (uint32_t d = 0; d < 4096; d++) {
        uint32_t mine = ysf_golay_encode(d);
        uint32_t ref = CGolay24128::encode24128(d);
        CHECK(mine == ref);
        if (mine != ref) { fprintf(stderr, "golay encode mismatch data %03X mine %06X ref %06X\n", d, mine, ref); return; }
    }
    fprintf(stderr, "golay encode: all 4096 values match MMDVM\n");
    /* Decode: every codeword with 0, 1 and 2 bit errors, and sampled
     * codewords with every 3-bit error pattern, must agree with MMDVM on
     * both the valid flag and the data. */
    unsigned patterns = 0;
    for (uint32_t d = 0; d < 4096; d++) {
        uint32_t code = CGolay24128::encode24128(d);
        for (unsigned w = 0; w < 3; w++) {
            /* weight w: all patterns with w bits among the low 24 bits, built
             * by choosing w positions out of 24 via nested loops for w<=2 */
            if (w == 0) {
                uint32_t back1 = 0, back2 = 0;
                bool v1 = ysf_golay_decode(code, &back1);
                bool v2 = CGolay24128::decode24128(code, back2);
                CHECK(v1 == v2 && (!v1 || back1 == back2));
                patterns++;
            } else if (w == 1) {
                for (unsigned i = 0; i < 24; i++) {
                    uint32_t err = code ^ (1u << i), back1 = 0, back2 = 0;
                    bool v1 = ysf_golay_decode(err, &back1);
                    bool v2 = CGolay24128::decode24128(err, back2);
                    CHECK(v1 == v2 && (!v1 || back1 == back2));
                    patterns++;
                }
            } else {
                for (unsigned i = 0; i < 24; i++)
                    for (unsigned j = i + 1; j < 24; j++) {
                        uint32_t err = code ^ (1u << i) ^ (1u << j), back1 = 0, back2 = 0;
                        bool v1 = ysf_golay_decode(err, &back1);
                        bool v2 = CGolay24128::decode24128(err, back2);
                        CHECK(v1 == v2 && (!v1 || back1 == back2));
                        patterns++;
                    }
            }
        }
    }
    for (unsigned t = 0; t < 64; t++) {
        uint32_t d = rnd_next() & 0xFFFu;
        uint32_t code = CGolay24128::encode24128(d);
        for (unsigned i = 0; i < 24; i++)
            for (unsigned j = i + 1; j < 24; j++)
                for (unsigned k = j + 1; k < 24; k++) {
                    uint32_t err = code ^ (1u << i) ^ (1u << j) ^ (1u << k), back1 = 0, back2 = 0;
                    bool v1 = ysf_golay_decode(err, &back1);
                    bool v2 = CGolay24128::decode24128(err, back2);
                    CHECK(v1 == v2 && (!v1 || back1 == back2));
                    patterns++;
                }
    }
    fprintf(stderr, "golay decode: %u error patterns agree with MMDVM\n", patterns);
}

static ysf_fich mkfich(unsigned fi, unsigned fn, unsigned dt, unsigned dev, unsigned mr, unsigned sc)
{
    ysf_fich f;
    memset(&f, 0, sizeof f);
    f.fi = (uint8_t)fi; f.fn = (uint8_t)fn; f.dt = (uint8_t)dt;
    f.dev = (uint8_t)dev; f.mr = (uint8_t)mr; f.sc = (uint8_t)sc;
    return f;
}

static void fich_checks(void)
{
    /* Ours -> MMDVM. */
    static const struct { unsigned fi, fn, dt, dev, mr, sc; } cases[] = {
        { 0, 0, 0, 0, 0, 42 }, { 1, 0, 0, 0, 0, 42 }, { 1, 5, 0, 0, 1, 0 },
        { 1, 7, 2, 1, 2, 127 }, { 2, 0, 1, 0, 0, 126 }, { 3, 3, 3, 1, 2, 1 },
    };
    for (unsigned t = 0; t < sizeof cases / sizeof *cases; t++) {
        ysf_fich f = mkfich(cases[t].fi, cases[t].fn, cases[t].dt, cases[t].dev, cases[t].mr, cases[t].sc);
        f.cm = 1; f.bn = 2; f.bt = 3; f.ft = 6; f.voip = 1;
        uint8_t frame[YSF_FRAME_BYTES];
        memset(frame, 0, sizeof frame);
        memcpy(frame, ysf_sync_bytes, YSF_SYNC_BYTES);
        ysf_fich_encode(&f, frame + YSF_SYNC_BYTES);
        CYSFFICH ref;
        CHECK(ref.decode(frame));
        if (!ref.decode(frame)) continue;
        CHECK(ref.getFI() == cases[t].fi);
        CHECK(ref.getCM() == 1);
        CHECK(ref.getBN() == 2);
        CHECK(ref.getBT() == 3);
        CHECK(ref.getFN() == cases[t].fn);
        CHECK(ref.getFT() == 6);
        CHECK(ref.getDT() == cases[t].dt);
        CHECK(ref.getMR() == cases[t].mr);
        CHECK(ref.getDev() == (cases[t].dev != 0));
        CHECK(ref.getDGId() == cases[t].sc);
    }
    /* MMDVM -> ours. */
    for (unsigned t = 0; t < sizeof cases / sizeof *cases; t++) {
        if (cases[t].dt != 0) continue;   /* MMDVM cannot set DT */
        CYSFFICH ref;
        ref.setFI((unsigned char)cases[t].fi);
        ref.setBN(2); ref.setBT(3);
        ref.setFN((unsigned char)cases[t].fn);
        ref.setFT(6);
        ref.setMR((unsigned char)cases[t].mr);
        ref.setVoIP(true);
        ref.setDev(cases[t].dev != 0);
        ref.setDGId((unsigned char)cases[t].sc);
        uint8_t frame[YSF_FRAME_BYTES];
        memset(frame, 0, sizeof frame);
        memcpy(frame, ysf_sync_bytes, YSF_SYNC_BYTES);
        ref.encode(frame);
        ysf_fich f;
        CHECK(ysf_fich_decode(frame + YSF_SYNC_BYTES, &f));
        CHECK(f.fi == cases[t].fi && f.fn == cases[t].fn && f.ft == 6);
        CHECK(f.bn == 2 && f.bt == 3 && f.mr == cases[t].mr && f.voip == 1);
        CHECK(f.dev == cases[t].dev && f.sc == cases[t].sc);
        CHECK(f.cm == 0 && f.dt == 0 && f.cs == 0 && f.sql == 0);
    }
    fprintf(stderr, "fich cross-check done\n");
}

static void dch_checks(void)
{
    uint8_t frame[YSF_FRAME_BYTES], csd1[20], csd2[20];
    memcpy(csd1, dest_cs, 10); memcpy(csd1 + 10, src_cs, 10);
    memcpy(csd2, down_cs, 10); memcpy(csd2 + 10, up_cs, 10);

    /* Ours -> MMDVM: header. */
    ysf_fich hf = mkfich(YSF_FI_HEADER, 0, YSF_DT_VD_MODE1, 0, 0, 42);
    ysf_frame_build(&hf, csd1, csd2, frame);
    {
        CYSFPayload p;
        CHECK(p.processHeaderData(frame));
        CHECK(p.getDest() && !memcmp(p.getDest(), dest_cs, 10));
        CHECK(p.getSource() && !memcmp(p.getSource(), src_cs, 10));
        /* MMDVM rewrote the frame in place: our decoder still reads CSD2. */
        ysf_fich f; uint8_t dch[20], dch2[20]; unsigned dlen = 0;
        CHECK(ysf_frame_decode(frame, &f, dch, dch2, &dlen) && dlen == 20);
        CHECK(!memcmp(dch, csd1, 20) && !memcmp(dch2, csd2, 20));
    }
    /* MMDVM -> ours: header. */
    memset(frame, 0, sizeof frame);
    memcpy(frame, ysf_sync_bytes, YSF_SYNC_BYTES);
    {
        ysf_fich f0 = mkfich(YSF_FI_HEADER, 0, YSF_DT_VD_MODE1, 0, 0, 42);
        ysf_fich_encode(&f0, frame + YSF_SYNC_BYTES);
        CYSFPayload p;
        p.writeHeader(frame, csd1, csd2);
        ysf_fich f; uint8_t dch[20], dch2[20]; unsigned dlen = 0;
        CHECK(ysf_frame_decode(frame, &f, dch, dch2, &dlen) && dlen == 20);
        CHECK(!memcmp(dch, csd1, 20) && !memcmp(dch2, csd2, 20));
    }
    /* Ours -> MMDVM: CC FN0 (V/D-1) and back with one flipped air bit. */
    ysf_fich cf = mkfich(YSF_FI_COMMUNICATION, 0, YSF_DT_VD_MODE1, 0, 0, 42);
    ysf_frame_build(&cf, csd1, NULL, frame);
    {
        CYSFPayload p;
        CHECK(p.processVDMode1Data(frame, 0));
        CHECK(p.getDest() && !memcmp(p.getDest(), dest_cs, 10));
        CHECK(p.getSource() && !memcmp(p.getSource(), src_cs, 10));
    }
    frame[YSF_SYNC_BYTES + YSF_FICH_BYTES + 3] ^= 0x10;   /* one air-bit error */
    {
        CYSFPayload p;
        CHECK(p.processVDMode1Data(frame, 0));
        CHECK(p.getDest() && !memcmp(p.getDest(), dest_cs, 10));
    }
    /* MMDVM -> ours: one DCH1 block. */
    memset(frame, 0, sizeof frame);
    memcpy(frame, ysf_sync_bytes, YSF_SYNC_BYTES);
    {
        ysf_fich f0 = mkfich(YSF_FI_COMMUNICATION, 0, YSF_DT_VD_MODE1, 0, 0, 42);
        ysf_fich_encode(&f0, frame + YSF_SYNC_BYTES);
        CYSFPayload p;
        p.writeDataFRModeData1(csd1, frame);
        uint8_t air[45], out[20];
        for (unsigned b = 0; b < 5; b++) memcpy(air + 9 * b, frame + YSF_SYNC_BYTES + YSF_FICH_BYTES + 18 * b, 9);
        CHECK(ysf_dch1_decode(air, out) && !memcmp(out, csd1, 20));
    }
    /* Ours -> MMDVM: DN CC FN0/FN1. */
    for (unsigned fn = 0; fn < 2; fn++) {
        ysf_fich df = mkfich(YSF_FI_COMMUNICATION, fn, YSF_DT_VD_MODE2, 0, 0, 42);
        uint8_t dch2[10];
        memcpy(dch2, fn == 0 ? dest_cs : src_cs, 10);
        ysf_frame_build(&df, dch2, NULL, frame);
        CYSFPayload p;
        CHECK(p.processVDMode2Data(frame, (unsigned char)fn));
        if (fn == 0) CHECK(p.getDest() && !memcmp(p.getDest(), dest_cs, 10));
        else CHECK(p.getSource() && !memcmp(p.getSource(), src_cs, 10));
    }
    /* MMDVM -> ours: DN, through its re-encode path. Build a DN frame, flip
     * one air bit, let MMDVM correct and rewrite it, decode it ourselves. */
    {
        ysf_fich df = mkfich(YSF_FI_COMMUNICATION, 1, YSF_DT_VD_MODE2, 0, 0, 42);
        uint8_t dch2[10];
        memcpy(dch2, src_cs, 10);
        ysf_frame_build(&df, dch2, NULL, frame);
        frame[YSF_SYNC_BYTES + YSF_FICH_BYTES + 1] ^= 0x04;
        CYSFPayload p;
        CHECK(p.processVDMode2Data(frame, 1));
        ysf_fich f; uint8_t dch[20]; unsigned dlen = 0;
        CHECK(ysf_frame_decode(frame, &f, dch, NULL, &dlen) && dlen == 10);
        CHECK(!memcmp(dch, src_cs, 10));
    }
    /* Ours -> MMDVM: Data FR header-style CC FN0 (CSD1/CSD2). The function
     * returns ret1 && (fn == 0), so FN0 is the frame whose return value
     * means "both DCHs decoded". Flip one air bit in each DCH: MMDVM must
     * correct and rewrite both in place, restoring the clean frame. */
    {
        ysf_fich df = mkfich(YSF_FI_COMMUNICATION, 0, YSF_DT_DATA_FR, 0, 0, 42);
        uint8_t clean[YSF_FRAME_BYTES];
        ysf_frame_build(&df, csd1, csd2, clean);
        memcpy(frame, clean, sizeof frame);
        frame[YSF_SYNC_BYTES + YSF_FICH_BYTES + 5] ^= 0x20;    /* one air-bit error in DCH-1 */
        frame[YSF_SYNC_BYTES + YSF_FICH_BYTES + 14] ^= 0x08;   /* one air-bit error in DCH-2 */
        CYSFPayload p;
        CHECK(p.processDataFRModeData(frame, 0));
        CHECK(p.getDest() && !memcmp(p.getDest(), dest_cs, 10));
        CHECK(p.getSource() && !memcmp(p.getSource(), src_cs, 10));
        CHECK(!memcmp(frame, clean, sizeof frame));
        ysf_fich f; uint8_t dch[20], dch2[20]; unsigned dlen = 0;
        CHECK(ysf_frame_decode(frame, &f, dch, dch2, &dlen) && dlen == 20);
        CHECK(!memcmp(dch, csd1, 20) && !memcmp(dch2, csd2, 20));
    }
    /* MMDVM -> ours: Data FR CC FN1 (CSD3 + DT1) via its writers. */
    {
        memset(frame, 0, sizeof frame);
        memcpy(frame, ysf_sync_bytes, YSF_SYNC_BYTES);
        ysf_fich f0 = mkfich(YSF_FI_COMMUNICATION, 1, YSF_DT_DATA_FR, 0, 0, 42);
        ysf_fich_encode(&f0, frame + YSF_SYNC_BYTES);
        uint8_t csd3[20], dt1[20];
        memcpy(csd3, "REM1      REM2      ", 20);
        memcpy(dt1, "DT1:0123456789ABCDE", 20);
        CYSFPayload p;
        p.writeDataFRModeData1(csd3, frame);
        p.writeDataFRModeData2(dt1, frame);
        ysf_fich f; uint8_t dch[20], dch2[20]; unsigned dlen = 0;
        CHECK(ysf_frame_decode(frame, &f, dch, dch2, &dlen) && dlen == 20);
        CHECK(!memcmp(dch, csd3, 20) && !memcmp(dch2, dt1, 20));
    }
    fprintf(stderr, "dch cross-check done\n");
}

int main(void)
{
    golay_checks();
    fich_checks();
    dch_checks();
    fprintf(stderr, "checks %u failures %u\n", checks, failures);
    assert(failures == 0);
    printf("PASS YSF MMDVM oracle: %u checks, our FICH/DCH/Golay agree with MMDVMHost\n", checks);
    return 0;
}
