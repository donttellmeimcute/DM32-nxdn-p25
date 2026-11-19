<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# YSF (Yaesu System Fusion) / C4FM: design, hardware fit, and plan

Status: phase 1 (RX data, host) done; TX host phase (M5a) done; firmware
port done 2026-11-19 (M4 RX + M5b TX + M5c TX voice, half-rate, in
`integration/current/base.patch`, host tested, pushed to main). No air
validation yet: the tap response (M2) is unmeasured, no real YSF
transmission has been received or sent, and the voice parameters are not
proven against a real capture. Nothing here is pushed anywhere.

Correction to the first draft of this document: YSF is 9600 **bits**/s at
4800 **symbols**/s (2 bits per symbol), not 9600 baud. The first draft's
sample-rate wall was computed from the wrong rate and is withdrawn below; the
8 kHz FM audio path does carry a 4800-symbol/s signal, and the firmware's own
fit functions accept it. The remaining work is validation, not a new sample
path.

## Air interface (verified)

Sources: Yaesu, "Amateur Radio Digital Standards", Rev. 1.02 (2015-07-29);
MMDVMHost (`YSFDefines.h`, `YSFFICH.cpp`, `YSFPayload.cpp`,
`YSFConvolution.cpp`, `AMBEFEC.cpp`, `CRC.cpp`, `YSFControl.cpp`);
DSD-FME (`src/ysf.c`, `src/dsd_frame_sync.c`, `include/dsd.h`). Secondary
sources were checked and rejected where they disagree with Yaesu/MMDVM (see
"Sources not used").

| Item | Value | Source |
|---|---|---|
| Modulation | C4FM, 9600 bit/s, **4800 symbols/s**, raised-cosine α = 0.2 | Yaesu §3.1(7), §3.2(3) |
| Bit order | Dibits MSB first ("starting from the leading bit") | Yaesu §3.2(2); MMDVM `BIT_MASK_TABLE` MSB-first |
| Dibit → symbol → deviation (Wide) | 00 = +1 = +900 Hz; 01 = +3 = +2700 Hz; 10 = −1 = −900 Hz; 11 = −3 = −2700 Hz | Yaesu Table 3-1 |
| Deviation (Narrow) | +1 = +450 Hz, +3 = +1350 Hz (and negatives) | Yaesu Table 3-1; FICH Dev bit selects Wide (0) / Narrow (1) |
| Max deviation | ±4950 Hz; symbol-rate tolerance ±5 ppm | Yaesu §3.3(7)(8) |
| Channel | 12.5 kHz (universally cited; Yaesu §3.1(6) only says "the stipulated amateur spacing", no figure) | oeradio.at, n6jet.com (secondary) |
| Frame | 960 bits / 100 ms: `[FS 40][FICH 200][payload 720]` | Yaesu §3.1(8), Fig. 4-1; MMDVM `YSF_FRAME_LENGTH_BYTES = 120` |
| Sequence | Header (HC) → Communication (CC, FN = 0…7) → Terminator (TC) | Yaesu §4; MMDVM `YSFControl.cpp` |
| Frame sync | One sync, `D471C9634D`, 40 bits, at bits 0–39 of **every** frame (HC/CC/TC). No separate FICH or data sync. | Yaesu §4.2.1(3); MMDVM `YSF_SYNC_BYTES[]`; DSD-FME `FUSION_SYNC` |
| Sync acquisition | Established on N1 = 1 reception (recommended); lost after N2 = 4 misses | Yaesu §4.7 |
| FICH | 200 bits transmitted; 32 bits of info | Yaesu Fig. 4-9, Table 4-1 |
| FICH fields (MSB→LSB) | `FI(2) CS(2) CM(2) BN(2) BT(2) FN(3) FT(3) rsv(1) Dev(1) MR(3) VoIP(1) DT(2) SQL(1) SC(7)` | Yaesu Table 4-1; MMDVM `YSFFICH.cpp` getters agree |
| FICH field meanings | FI: 0=Header, 1=Communication, 2=Terminator, 3=Test. DT: 0=V/D-1, 1=Data FR, 2=V/D-2 (DN), 3=Voice FR (VW). CM: 0=Group/CQ, 1=Radio ID, 3=Individual. MR: 0=Direct, 1=Downlink (uplink free), 2=Downlink (uplink busy) | Yaesu Table 4-1 |
| FICH FEC chain | 32 info + CRC-16 (poly x¹⁶+x¹²+x⁵+1, init 0, invert at end) = 48 → 4× extended Golay (24,12,8) = 96 → +4 zero tail bits → convolutional R=1/2, K=5 (G1 = 1+x³+x⁴, G2 = 1+x+x²+x⁴) = 200 → interleave M=5 dibits, depth N=10 | Yaesu §4.5(1); MMDVM `YSFFICH.cpp`, `YSFConvolution.cpp` (Viterbi, 16 states) |
| DCH, V/D-1 / Data FR / HC / TC | 360 bits = 160 data + CRC-16 + 4 tail → convolutional K=5 → 360, interleave M=9 dibits, N=20 | Yaesu §4.5(2), Fig. 4-30; MMDVM `INTERLEAVE_TABLE_9_20` |
| DCH, V/D-2 (DN) | 200 bits = 80 data + CRC-16 + 4 tail → convolutional → 200, interleave M=5, N=20 | Yaesu Fig. 4-31; MMDVM `INTERLEAVE_TABLE_5_20` |
| Callsigns (CSD) | Each callsign 10 ASCII characters = 10 bytes (80 bits), space (0x20) padded. CSD1 = Dest + Src; CSD2 = Downlink + Uplink; CSD3 = Rem1–Rem4 (5 bytes each) | Yaesu Table 4-2; MMDVM `YSF_CALLSIGN_LENGTH = 10` |
| CSD rotation in DN | FN0=Dest, FN1=Src, FN2=Down, FN3=Up, FN4=Rem1+2, FN5=Rem3+4, then DT1/DT2 | Yaesu Table 4-4 |
| CSD rotation in V/D-1 | FN0=CSD1, FN1=CSD2, FN2=CSD3, then DT1–DT5 | Yaesu Table 4-3 |
| Payload per 20 ms block, V/D-1 (DT 0) | DCH 72 + VCH 72 | Yaesu Fig. 4-8; MMDVM `YSFPayload.cpp` |
| Payload per 20 ms block, DN (DT 2) | DCH 40 + VCH 72 + VeCH 32 | Yaesu Fig. 4-12 |
| Payload per 20 ms block, Data FR (DT 1) | DCH-1 72 + DCH-2 72 (no voice) | Yaesu Fig. 4-15 |
| Payload per 20 ms block, VW (DT 3) | VCH 144 (no DCH, except CC0 sub-header: 5×72 DCH + 72 reserved + 2×144 VCH) | Yaesu Fig. 4-21, 4-22 |
| Voice, half-rate (V/D-1, DN) | 49 bits voice + 23 bits FEC = 72 bits / 20 ms; 2450 + 1150 = 3600 bit/s; AMBE+2 Enhanced Half-Rate | Yaesu §4.2.1(7) |
| Voice, half-rate FEC | V/D-1: the standard AMBE channel frame — Golay(24,12) + Golay(23,12) + 25 unprotected bits (the DMR tables) | MMDVM `AMBEFEC::regenerateYSFDN` |
| Voice, DN extra layer | The 49 bits split 27+22; the first 27 are tripled (81 bits) + 22 = 103 + 1 zero bit = 104 bits, whitening, 26×4-bit interleave, split across VCH 72 + VeCH 32 | Yaesu §4.5(3), Fig. 4-32; MMDVM `processVDMode2Audio` (majority vote on 27 triplets) |
| Voice, full-rate (VW) | 88 bits voice + 56 bits FEC = 144 bits / 20 ms; 4400 + 2800 = 7200 bit/s; AMBE+2 Enhanced Full-Rate | Yaesu §4.4(7) |
| Voice, VW in MMDVM | Regenerated with `regenerateIMBE` (144-bit frame, 3× Golay(23,12) + Hamming, with PRN) — MMDVM treats VW voice as IMBE-style | MMDVM `AMBEFEC.cpp` |
| Whitening | PN(9,5), 9-bit register, initial value `111001001` (0x1C9), reset per unit, XOR from the top bit. Applies to DCH and the DN voice block (VCH+VeCH). **Not** to FS or FICH. | Yaesu §4.5(2)(b), Fig. 4-28; MMDVM `WHITENING_DATA` (20 bytes) in `YSFPayload.cpp`; DSD-FME `pn95[512]` from seed 0x1C9 |
| DG-ID | FICH field SC, 7 bits (byte 3, bits 6–0): 0 = none, 1–126 = SQL codes, 127 = Break Out. SQL bit (byte 3, bit 7): 0 = SQL enabled, 1 = disabled. | Yaesu Table 4-1; MMDVM `getDGId() = m_fich[3] & 0x7F` |
| DG-ID, user radios | 00–99 on Yaesu equipment; 00 = receive all. Independent of the callsign: DG-ID is the FICH squelch; callsigns travel in CSD/DCH. | Yaesu FTM-300DR / FT-70DR manuals |
| MMDVMHost and DG-ID | Does not filter by DG-ID: logs it and forwards it. The "invalid access attempt" in `YSFControl.cpp` is a own-callsign check (`m_selfOnly`), not DG-ID. | MMDVM `YSFControl.cpp` |
| Test vectors | **None public and verifiable found.** MMDVMHost has no test directory or files; DSD-FME has no captures or vectors; no published example YSF frames found. | Research, 2026-11-18 |

### Sources not used

- Lidstone's YSF wiki (patrick.lidstone.net/wiki/protocols/ysf/): describes
  DN as 5×(DCH 72 + VCH 72) with a 20-character CSD per frame — that is
  actually the V/D-1 geometry (Yaesu Fig. 4-8), not DN (Fig. 4-12:
  40+72+32). Its whitening (7-bit LFSR x⁷+x⁴+1, seed 0x7F) and CRC (init
  0xFFFF) also differ from Yaesu §4.5 + MMDVM (PN(9,5), seed 0x1C9; CRC
  init 0 + final inversion). The wiki itself calls itself an unverified
  reconstruction (§8). Follow Yaesu + MMDVM, not Lidstone.
- TAPR DCC 2017 presentation (K9EQ, via archive.org): useful summary, but
  its OCR is unreliable for exact figures; not a primary source.
- Minor MMDVM discrepancy noted, not resolved: MMDVM implements MR as
  2 bits (0/1/2) and does not expose CS or SQL; the spec says MR is 3 bits.
  The prototype follows the spec layout and notes where MMDVM differs.
- Whitening table equivalence VERIFIED (host prototype, 2026-11-18): the
  PN(9,5) LFSR with seed 0x1C9 generates MMDVM's fixed 20-byte
  `WHITENING_DATA` table byte for byte (`93 D7 51 21 9C 2F 6C D0 EF 0F F8
  3D F1 73 20 94 ED 1E 7C D8`). Right-shifting 9-bit register, output bit 0,
  feedback bit0 ^ bit4 into bit 8, output bits packed MSB-first. The check
  runs in `tests/ysf/test_ysf_modem.c` (`whitening` case).

## Hardware fit — measured from this repo

### The sample path fits (corrected)

| Path | Rate | What it means for 4800 symbols/s |
|---|---|---|
| FM RX RAM copy (modem's copy of the discriminator audio) | 8 kHz, two 256-sample halves, 32 ms each (measured over 95 changes) | 8000/4800 = 5/3 = 1.67 samples per symbol. Thin (NXDN had 10/3), but the signal's baseband (RRC 0.2 at 4800 symbols/s, ±2880 Hz) fits inside the 4 kHz Nyquist. |
| FM TX RAM | 8 kHz, 256 samples per half | 5-phase polyphase modulation at 5/3 samples per symbol, same structure as NXDN's 10-phase table. |
| Native 4FSK demodulator / modulator (HR_C7000) | 4800 symbols/s fixed | Exactly YSF's symbol rate. The chip demodulates C4FM at this rate for P25. |
| Chip internal sampling | 38.4 kHz (AK2401 note) | Not needed for the software route; no memory tap established, and none is needed now. |

The firmware's own generic soft modem states the requirement exactly
(`build/current/core/air/air_softmodem.c`, `rate_ok`): a profile needs a tap
of at least `sym_rate * 5 / 3` — 8000 Hz for 4800 symbols/s, met exactly by
the DM32 tap — and at most twice the filtered rate; and a band reaching at
least `sym_rate * 11 / 20` — 2640 Hz, met by the tap's 0–3000 Hz
(`HRC7000_AIR_RATE_HZ` = 8000, `HRC7000_AIR_RX_HI_HZ` = 3000,
`HRC7000_AIR_RX_LO_HZ` = 0). So `air_softmodem_rx_fit` for a 4800-symbol/s
YSF profile returns a fit on this board. The margins are zero on rate and
360 Hz on band: the host simulation must show the demodulator working at
1.67 samples per symbol with the tap's real band, and the radio must confirm
the tap's actual frequency response (de-emphasis / high-pass / gain), exactly
as NXDN's hardware checklist does (`docs/nxdn-modem.md`, "What the radio
must confirm").

### What already exists and fits

- **Generic soft modem** (`build/current/core/air/air_softmodem.c`,
  `air_softmodem.h`): resamples the tap to 5 samples per symbol, integrate
  and dump, finds the profile's sync patterns by least-squares fit for
  timing, level and offset, hands out symbols with soft values. Cost is per
  filtered sample: 24,000 a second at 4800 symbols/s — the same cost class
  as the existing modes, not a new one. Rate-generic: a YSF profile is a new
  `air_profile_t` (`sym_rate` is a `uint16_t`; the profile comment lists
  4800/2400/1200 because those are the modes that exist, not because the
  code limits it).
- **Sync length fits**: `AIR_SYNC_SYMS_MAX` is 24 symbols (48 bits); the
  40-bit YSF sync is 20 symbols, inside the 8..24 range. The sync recurs
  every 480 symbols (100 ms), so `sync_every_syms` = 480. The framer
  (`core/air/air_framer.c`) and session (`air_session.c`) are mode-generic.
- **Native TX fits (later phase)**: `hrc7000_air_tx_fit` accepts 4800
  symbols/s; YSF's pulse is RRC 0.2, the same shape as the chip's own DMR
  pulse, so no `AIR_FIT_SHAPE_APPROX`; deviation 2700/1350 Hz is inside the
  100..6000 Hz window, so no `AIR_FIT_DEV_APPROX`.
- **NXDN precedent** (`src/nxdn/nxdn_modem.c`, `docs/nxdn-modem.md`):
  integer only (CK803S has no FPU), matched filter (RRC 0.2) + Gardner
  timing + level/offset tracking, host channel simulation
  (`tests/nxdn/nxdn_channel.c`: FM at 48 kHz, 12.5 kHz channel filter,
  ideal discriminator, audio low-pass, decimation). The YSF host prototype
  mirrors this structure at 8 kHz / 4800 symbols/s (5/3 samples per symbol).
- **CPU**: CK803S, no FPU. The NXDN modem costs about 220k multiply-adds a
  second for both filters at 8 kHz / 2400 symbols/s, "small next to the
  AMBE+2 vocoder" (`docs/nxdn-modem.md`). A YSF demodulator at 4800
  symbols/s is roughly twice the symbol work of NXDN at the same 8 kHz
  input — an estimate to be measured on target with `tools/cpu_bench.py`,
  not a claim. The vocoder is expected to dominate, as it does for NXDN.
- **RAM/flash are tight**: the early on-radio application measured 458,852
  bytes of flash and 188,464/211,456 bytes of main RAM used, leaving 22,992
  bytes in that region, with 8,144 bytes of heap free after the audio task
  started (`docs/receive-audio.md` / `docs/codec.md` context). A YSF mode
  must budget its tables and state against that; the host prototype's tables
  are not the firmware's until measured.

### Vocoder (the second gate, now narrower)

- DMR voice needs DVSI's AMBE+2 codec. It is proprietary; RadioFoundry never
  contains it. Each radio runs the codec from its own stock firmware, merged
  on the user's machine (`build/current/docs/dev/codec.md`).
- The firmware's interface (`build/current/hal/hal_vocoder.h`) is **49
  voice parameter bits** (`VOCODER_BITS`), one per byte, MSB first; the
  AMBE+2 FEC and interleaving to and from the 72-bit air frame are done in
  open code in `core/dmr/ambe`. The blob exposes parameters, not air frames.
- **Half-rate YSF voice matches that interface**: 49 voice bits per 20 ms,
  exactly `VOCODER_BITS`, with the channel FEC (Golay(24,12) +
  Golay(23,12) + 25 unprotected bits) done in open code from tables the
  firmware already has for DMR. So half-rate voice (V/D-1 and DN) via the
  existing blob is plausible; DN's extra layer (triplication, whitening,
  26×4 interleave) is open code too. This still needs a voice vector or a
  real capture to prove: the parameter bits must be the same AMBE+2
  parameters the blob expects, not just the same count.
- **Full-rate VW voice does not match**: 88 voice bits per 20 ms. MMDVM
  regenerates it IMBE-style (`regenerateIMBE`). The repo has a full-rate
  IMBE codec (`docs/codec.md`: nostar/imbe_vocoder, host-only, cross-compiled
  for CK803S, never run on the radio). VW voice is therefore a separate
  problem from half-rate voice, and it is out of the RX-data phase.
- There is no AMBE+2 software decoder in this repo: mbelib is a host-only
  **IMBE** decoder and test oracle, explicitly not linked into the embedded
  codec (`docs/licenses.md`); BLIP25 documents a fixed-point Rust core but
  its build and footprint on this CPU have not been demonstrated.
- Precedent for sharing: the vocoder interface already lends the codec's
  working memory to another voice path while no DMR voice runs (P25's
  decoder), so a YSF voice path borrowing the same blob is architecturally
  consistent — if the parameter interface matches.

## Transmission (M5a — host, done 2026-11-18)

The modulator already existed in phase 1 (`ysf_mod`: dibits -> RRC 0.2
polyphase -> 8 kHz samples, the mirror of `nxdn_mod`). This phase adds the
transmitter above it, `src/ysf/ysf_tx.{h,c}`: a sequence builder plus a
streamer, integer only, no allocation, reusable by the firmware port.

```
ysf_tx_config (callsigns, DT mode, DG-ID, Wide/Narrow, data + voice callbacks)
  -> ysf_tx_frame_build()  (FICH + DCH rotation + voice blocks, one frame)
  -> ysf_frame_build()     (sync + FICH + DCH encode, reused from RX)
  -> ysf_tx_dibits() / ysf_tx_samples()  (ysf_mod, RRC 0.2, 4800 sym/s)
```

### Sequence

One transmission is header (FI 0) -> eight communication frames (FI 1,
FN 0..7) -> terminator (FI 2): 10 frames, 100 ms each, 1.0 s per cycle.
`cycles` repeats the communication block for longer transmissions
(2 cycles = 18 frames). FICH on every frame: DT = configured mode,
Dev = Wide/Narrow, SC = DG-ID, CM/MR/CS/SQL/VoIP from the config;
FT/BN/BT are 0, as MMDVM leaves them on transmit (its `YSFControl.cpp`
only ever sets FT 0 and derives FN/BN from received frames; no source
states what a radio should put in FT/BN/BT, so 0 is the conservative
choice, to be confirmed against a real radio's frames).

DCH rotation, verified against MMDVM's `processVDMode1Data` /
`processVDMode2Data` / `processDataFRModeData` and Yaesu Tables 4-3/4-4:

| Mode | FN0 | FN1 | FN2 | FN3 | FN4 | FN5 | FN6 | FN7 |
|---|---|---|---|---|---|---|---|---|
| V/D-1 (DT 0) | CSD1 | CSD2 | CSD3 | DT1 | DT2 | DT3 | DT4 | DT5 |
| DN (DT 2) | Dest | Src | Down | Up | Rem1+2 | Rem3+4 | DT1 | DT2 |
| Data FR (DT 1) | CSD1/CSD2 | CSD3/DT1 | DT2/DT3 | DT4/DT5 | DT6/DT7 | DT8/DT9 | DT10/DT11 | DT12/DT13 |
| VW (DT 3) | — | — | — | — | — | — | — | — |

(Data FR carries two DCH1s per frame: DCH-1 in bytes 0..8 of each block,
DCH-2 in bytes 9..17. The header and terminator always carry CSD1 + CSD2,
whatever the mode.) The Data FR second DCH needed a small extension of
`ysf_frame_build` / `ysf_frame_decode` (`dch2_or_null` is now the DCH-2 of
a Data FR communication frame too); it is cross-checked against MMDVM in
both directions (`writeDataFRModeData1/2`, `processDataFRModeData`).

### Voice payloads, and what is NOT voice yet

The transmitter takes voice bytes through a callback: 9 bytes per 20 ms
block in V/D-1 (VCH), 13 in DN (VCH 9 + VeCH 4), 18 in VW. With no
callback the blocks are all zero — a deterministic test payload, **not** a
valid AMBE silence frame. The loopback tests verify those bytes survive
the channel bit-exact (0 errors clean and at C/N 10 dB in the fixed-seed
runs), which proves the voice *path*; it does not prove any voice
*content*. What is still missing for real voice:

- The half-rate channel frame: 49 AMBE+2 parameter bits -> 72-bit VCH
  (Golay(24,12) + Golay(23,12) + 25 unprotected bits, the DMR tables, per
  MMDVM `AMBEFEC::regenerateYSFDN`). The mapping is documented, not
  implemented: it needs a known-parameter vector or a real YSF voice
  capture to prove the bit order, and the parameter bits must be the ones
  the DVSI blob expects (same open question as RX voice, M3).
- The DN extra layer (triplication of the first 27 bits, whitening,
  26x4 interleave across VCH + VeCH) is open code, also unproven without a
  capture.
- Full-rate VW voice (88 bits / 20 ms) is the IMBE-style path, separate
  from the blob's 49-bit interface.
- The VW CC0 sub-header (Yaesu Fig. 4-22: 5x72 DCH + 72 reserved + 2x144
  VCH) is not built: no source states what its DCH carries.

### Deviation and timing

- Wide: outer +2700 Hz, inner +900 Hz. Narrow (FICH Dev = 1): the
  modulator gain is halved (+1350/+450 Hz); the loopback measures peak
  |sample| ratio wide/narrow 1.98. The FICH Dev bit and the gain are set
  together by `ysf_tx_init`, so they cannot disagree.
- No preamble: the first symbols are the frame sync, so the PA must be up
  before the first sample leaves the modulator, and `ysf_tx_init` preloads
  the header frame for that. The streamer flushes the modulator's RRC
  tail after the terminator (`ysf_mod_flush`); a transmission is 8000
  samples per 10-frame cycle at 8 kHz (4800 dibits x 5/3), plus the tail.
- `ysf_tx_samples` is safe to call with small buffers (7-sample chunks
  produce byte-identical output): leftover samples are held in the
  transmitter, never dropped — the firmware's TX RAM halves are 256
  samples, so this matters.

### Validation (host)

`tests/ysf/test_ysf_tx.c` (1956 checks, 0 failures, ASan/UBSan):
frame-by-frame FICH/DCH/voice checks for all four modes, wide and narrow;
silence (NULL voice callback -> zero blocks); streamer fidelity (dibits,
8000 samples per cycle, small-chunk identity, 2 cycles); narrow peak
ratio 1.98; loopback modulator -> FM channel -> demodulator -> frame
decoder for V/D-1, DN, Data FR (clean, C/N 20, C/N 10 dB), VW (clean)
and V/D-1 narrow (clean, C/N 20 dB): every frame's sync, FICH fields,
callsigns and data units exact, voice-region bit errors 0 in all runs
(bounds: clean 0, C/N 20 <= 16-24, C/N 10 <= 144-208 over the
transmission). The MMDVM oracle gained the Data FR cross-check
(1,366,637 checks total, 0 failures).

## Port to the radio (M5b — ported 2026-11-19, not validated on air)

The port went in through `integration/current/base.patch` as `core/ysf/`
(`ysf_frame`, `ysf_voice`, `ysf_rx`, `ysf_engine`, `ysf_tx` sequence builder
only), behind `CONFIG_YSF` (defaults to `CONFIG_DMR`), with host tests
`tests/host/ysfTest.c`, `ysfEngineTest.c`, and the YSF case in
`airSoftmodemTest.c`. RX is the generic `air_softmodem` + `air_framer` +
frame layer, as designed.

TX took a different route than this section first planned: the current
build's NXDN TX already goes through the HR_C7000 **native 4FSK slot
engine** via provider symbols, not the TX RAM route, so YSF follows it —
the engine queues the built frame's dibits through `air_session_tx` and the
host `ysf_tx` streamer (`ysf_tx_init` / `ysf_tx_samples`, `ysf_mod`) is not
ported. The TX RAM measurements below therefore do not apply as written;
what is still unmeasured is the native engine's deviation on air (outer
level 2700 Hz wide, 1350 Hz narrow) and the occupied bandwidth / eye, to
confirm on a service monitor. The frame timing (100 ms per frame, 10 frames
per cycle, 1.0 s) and the PTT / PA timing (the PA must be up before the
first symbols are consumed, or the sync is lost) still hold.

Air validation for M5b: service monitor (deviation, occupied bandwidth,
eye), a second YSF radio or MMDVM hotspot receiving the transmission
(FICH, callsigns, DG-ID), and DSD+ as the reference decoder — the same
ladder NXDN TX went through, which found real problems the host tests
could not (off-air block distortion, no decoded frames with the
microphone disconnected). No working YSF TX is claimed until then.

What is still unmeasured on the native route:

| Item | What the radio must confirm |
|---|---|
| Deviation on air | Outer level 2700 Hz wide, 1350 Hz narrow, on a service monitor; the native engine's scale is the chip's, not the host's 2 LSB/Hz. |
| RRC pulse shape / occupied bandwidth | Confirm the occupied bandwidth / eye on a service monitor or Pluto IQ; a closed eye means the shape or the scale is wrong. |
| PTT / PA timing | No preamble; the header is the first frame queued. The PA must be up before the first symbols are consumed, or the sync is lost. Measure the PA rise against the first chunk. |

## Architecture proposed (reception)

```
Air (4800 sym/s C4FM, 12.5 kHz)
  -> FM RX RAM copy, 8 kHz (exists; 5/3 samples per symbol)
  -> air_softmodem (5 samp/sym, sync fit, soft symbols)  OR  ysf_demod
     (NXDN-style matched filter + Gardner, ported later)
  -> air_framer (YSF sync D471C9634D, frame boundaries)
  -> ysf frame layer (FICH decode: Golay + Viterbi + deinterleave + CRC;
     DCH decode: deinterleave + Viterbi + whitening + CRC; callsign, DG-ID)
  -> AMBE+2 half-rate (blob via hal_vocoder, if parameters match) -> PCM -> speaker
```

Host prototype (phases 1 + 5a): the same chain above the tap, in `src/ysf/` +
`tests/ysf/` + `tools/test_ysf.py`, mirroring `src/nxdn/`:
`ysf_modem.c` (C4FM demodulator, integer, RRC matched filter + Gardner +
level tracking, tables from `tools/gen_ysf_tables.py`), a YSF frame encoder
for test vectors (sync + FICH chain + DCH chain, ported from MMDVMHost, not
re-derived from prose), `ysf_tx.c` (transmitter: sequence builder +
streamer over the same modulator), and a channel model mirroring
`tests/nxdn/nxdn_channel.c` (FM at 48 kHz, 12.5 kHz filter, discriminator,
noise by C/N, decimation to 8 kHz). The test vectors are generated by our
own encoder from the spec, so they prove the demodulator and decoder against
themselves — not against real air captures. That limitation is stated in the
tests, not hidden. No public YSF test vectors exist.

Firmware integration (later, gated): a YSF `air_profile_t` (4800, 4 levels,
RRC 0.2, YSF sync, `sync_every_syms` 480) + the generic soft modem + framer
+ a `core/ysf` frame layer, ported through
`integration/current/base.patch` like NXDN was. It needs the tap's frequency
response measured first (M2).

## Milestones

1. **M1 — RX data, host only (this phase).** Spec confirmed from sources;
   host C4FM demodulator at 4800 symbols/s from 8 kHz samples; sync
   acquisition; FICH and DCH decode (callsign, DG-ID) from generated
   vectors, clean and with noise. No voice, no firmware change.
2. **M2 — tap response on the radio.** Measure the RX RAM copy's frequency
   response (tones at 50 Hz–3 kHz, de-emphasis / high-pass / gain / clipping),
   as NXDN's checklist does. Go/no-go for the on-radio port is the measured
   response, not the fit function.
3. **M3 — RX voice, host.** YSF half-rate voice-frame FEC/de-interleave in
   open code; confirm whether the DVSI blob's 49-bit parameter interface
   decodes YSF voice frames (needs a YSF voice capture or a
   known-parameter vector); PCM out in the host harness. VW voice is a
   separate item (IMBE-style path). Mapping done 2026-11-19 and in the
   firmware port: V/D-1 is the DMR AMBE+2 frame (`ambeFec` reused, verified
   against MMDVMHost code on 19,917/20,000 random trials), DN is the
   triplet/whitening/interleave layer (`core/ysf/ysf_voice.c`); loopback
   host-tested, but the blob-parameter question is still open — no real
   capture yet.
4. **M4 — RX on the radio.** Port M1+M3 through `base.patch` behind the
   measured tap; validate against a real YSF transmission (another radio or
   MMDVM) and with DSD+ as reference decoder. Port done 2026-11-19
   (`core/ysf/`, `CONFIG_YSF`, host tests green); air validation pending —
   M2 is still unmeasured and no real YSF transmission has been received.
5. **M5a — TX, host (this phase, done 2026-11-18).** `ysf_tx` sequence
   builder + streamer (HC -> CC FN 0..7 -> TC) in all four modes, Wide and
   Narrow, with data and voice callbacks; loopback against the phase-1
   demodulator clean and at C/N 20/10 dB (1956 checks); Data FR second DCH
   cross-checked against MMDVMHost (oracle 1,366,637 checks). Voice
   payloads are test bytes, not AMBE.
6. **M5b — TX on the radio (ported 2026-11-19, not validated on air).**
   Port `ysf_tx` through `base.patch` onto the native 4FSK slot engine
   (not the TX RAM route first planned — the current build's NXDN TX
   already uses the native engine); calibrate deviation and occupied
   bandwidth on air; validate with a service monitor, a second YSF radio
   / MMDVM hotspot, and DSD+. Host tests green; the ARM build is verified
   by CI after the push.
7. **M5c — TX voice (ported 2026-11-19, not proven on air).** Half-rate
   AMBE channel frame (49 -> 72 bits) and the DN extra layer, in the
   firmware port and loopback host-tested; proven against a real YSF voice
   capture or a known-parameter vector is still pending. VW full-rate voice
   is a separate item (IMBE-style path).

## Risks and unverified assumptions

| Risk | Why it matters | What settles it |
|---|---|---|
| 1.67 samples per symbol is too thin for the demodulator | The soft modem's interpolator and Gardner loop were validated at 3.33 (NXDN) and at 5 resampled samples per symbol; the input rate is at the fit boundary | M1 simulation at C/N sweep; M2 tap measurement |
| Tap's real frequency response (de-emphasis / high-pass) | Uncompensated de-emphasis or high-pass breaks 4FSK reception outright (NXDN tables: frame error 1.00 for either alone) | M2 tones; compensations already exist in the NXDN modem to copy |
| YSF half-rate voice parameters differ from the blob's | Same bit count does not prove the same parameters; voice would decode as garbage | M3 voice vector / real capture |
| VW voice needs the IMBE path | Full-rate voice is a second vocoder problem, not covered by the blob's 49-bit interface | M3, separate item |
| No public YSF test vectors exist | Vectors are self-generated; the first real proof is an air capture, not the host tests | M4, with DSD+ as reference decoder |
| Whitening table vs LFSR sequence | Settled by the host prototype (2026-11-18): the PN(9,5) LFSR with seed 0x1C9 generates MMDVM's 20-byte `WHITENING_DATA` byte for byte; see the whitening row in the spec table above | — |
| Host prototype (2026-11-18) | Modem + frame layer + channel tests pass on host (`tools/test_ysf.py`: 823 checks; MMDVM oracle `tests/ysf/test_ysf_mmdvm.cc`: 1,366,629 checks, including Golay encode all 4096 values and decode over 1,362,432 error patterns). The oracle also caught a real bug: the Golay decoder missed three-data-bit error patterns (220 per codeword), fixed with a fourth search stage. Sync is accepted with up to 4 bit errors (`YSF_SYNC_MAX_ERRORS`); the FICH/DCH CRCs are the real frame validators | Air capture (M4); firmware port done 2026-11-19, see the row below |
| TX host (2026-11-18, M5a) | `ysf_tx` sequence builder + streamer, all four modes, Wide/Narrow, loopback clean and at C/N 20/10 dB (`tests/ysf/test_ysf_tx.c`: 1956 checks); Data FR second DCH cross-checked against MMDVM both directions (oracle now 1,366,637 checks). Voice blocks are test bytes: the path is proven, the content is not AMBE | M5b (radio TX) and M5c (TX voice) |
| TX deviation scale on air | Host gain is 2 LSB/Hz; the radio's TX RAM scale is unmeasured, as NXDN's was | M5b service-monitor measurement |
| FT/BN/BT on transmit | Sent as 0 (MMDVM's TX behaviour); no source pins what a radio should send | Air capture of a real Yaesu transmission (M4/M5b) |
| VW CC0 sub-header | Not built: no source states what its DCH carries | Yaesu spec detail or a real VW capture |
| TX voice (AMBE 49 -> 72) | Mapping documented from MMDVM `regenerateYSFDN` and implemented in the firmware port (`core/ysf/ysf_voice.c`, loopback host-tested); the parameter bits must match the DVSI blob's | M5c, needs a voice capture or known-parameter vector |
| RAM/flash headroom | A YSF mode plus its tables may not fit the measured headroom | Link-time budget (`tools/resource_budget.py`) at port time; the port's engine struct is ~1.2 KB (caller-allocated) plus the frame/voice tables — the ARM link is verified by CI after the push |
| Firmware port (2026-11-19) | `core/ysf/` in `base.patch` behind `CONFIG_YSF`: RX (soft modem + framer + frame layer), TX (native slot engine), half-rate voice both ways, DG-ID squelch. Host tests green: `ysfTest` 5375 checks, `ysfEngineTest` 81 checks, `airSoftmodemTest` 20017 checks, full host suite run (one pre-existing failure: `splitDuplexTest` segfaults on the pre-YSF tree too). Two real bugs found and fixed in the port: the RX layer passed packed framer symbols to a converter expecting unpacked dibits (0 frames heard), and the Golay decoder rejected correctable patterns whose error included the extension parity bit. TX is the native slot engine, not the TX RAM route first planned | CI firmware build (ARM) after the push; air validation (M2, M4, M5b, M5c) |
| MMDVM MR field is 2 bits, spec says 3 | Affects the FICH layout if the spec's third MR bit carries meaning on air | Air capture (M4); follow the spec until then |

## Sources

- Yaesu, "Amateur Radio Digital Standards", Rev. 1.02 (2015-07-29):
  https://yaesu.com/Files/DFDF4D53-1B78-7375-E6C7A6E4B192BE13/Yaesu_Amateur%20Radio%20Digital%20Specs_1V02_EN-GB.pdf
- MMDVMHost (github.com/g4klx/MMDVMHost): `YSFDefines.h`, `YSFFICH.cpp`,
  `YSFPayload.cpp`, `YSFConvolution.cpp`, `AMBEFEC.cpp`, `CRC.cpp`,
  `YSFControl.cpp`.
- DSD-FME (github.com/lwvmobile/dsd-fme): `src/ysf.c`,
  `src/dsd_frame_sync.c`, `include/dsd.h`.
- This repo: `docs/nxdn-modem.md` (NXDN modem, channel model, hardware
  steps), `docs/modem.md` (FM sample access),
  `build/current/docs/spec/modem-hrc7000.md` 8.5 (FM RX audio copy, 8 kHz),
  10 (air interface provider, 4800/2400 profiles),
  `build/current/core/air/air_softmodem.c` (`rate_ok`),
  `build/current/hal/hal_air.h` (profile, sync limits),
  `build/current/hal/hal_vocoder.h` (49-bit AMBE+2 parameter interface),
  `build/current/docs/dev/codec.md` (DVSI blob, per-radio merge),
  `docs/codec.md`, `docs/licenses.md` (mbelib host-only IMBE oracle).
