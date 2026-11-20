# Resources of the multi-mode image

## November 17, 2026: AES receive and what the screen says (patches 0047 and 0048)

The release configuration (`--speed-codec --stack-check --lean --trunk-tx`), built with the script before and after:

| Build | Image | RAM | Free RAM |
|---|---:|---:|---:|
| Series through `0046` | 557,044 | 207,648 | 3,808 |
| With `0047` and the P25 receive changes | 558,020 | 207,688 | 3,768 |
| With `0048` (DMR AES receive) | 558,620 | 207,760 | 3,696 |

`0047` costs 976 bytes of flash and 40 of RAM: the call state the P25 voice layer keeps (seven bytes, rounded up), the radio
core's view of it, the screen text and its translations. `0048` costs 600 bytes of flash and 72 of RAM: the DMR caller's
view of the PI header (FID, state), the burst-loss bookkeeping of the AES stream and the DMR reason line. Zero compiler
warnings, with AES on and off; the static stack check passes with the same margins (least free: idle task 136 bytes; UI task
248, P25 worker 3,884). Not run on a radio.

## November 17, 2026: no diagnostics page in the image

`tools/build_radiofoundry.py` builds with `CONFIG_DIAG=n` unless given `--diag`. The release configuration
(`--speed-codec --stack-check --lean --trunk-tx`, the series through `0046`), built with the script both ways:

| Build | Image | RAM | Free RAM |
|---|---:|---:|---:|
| With the page (`--diag`) | 561,068 | 209,016 | 2,440 |
| Default, without it | 557,044 | 207,648 | 3,808 |

The page costs 4,024 bytes of flash and 1,368 bytes of RAM. Most of the RAM is `hrc_dmr_stats_line` (960 bytes of code
the linker places in RAM, called only from the page's lines); the rest is `dm32_diag_line` (968 bytes of flash),
`ui_draw_diag` (444) and the counters they read. Zero compiler warnings in both; the static stack check passes with
the same margins (least free: idle task, 136 bytes). Not run on a radio.

What goes with it: Tools > Diagnostics (the rows after it move up one number); the board's raw readings (battery ADC,
RSSI and noise registers, FD6818 write count, host UART counters, SPI flash holder and ID, calibration source) and the
DMR modem counters, on the screen and in the debug link's `diag` command, which still gives the radio summary and the
DMR call lines; the `DIAG` entry of the feature list the radio reports. The measurement that sizes the heap and the
stacks (`tasks`, `CONFIG_TASK_STACKS`) is a separate switch and stays.

## November 17, 2026: AM gain loop (patch 0046)

The release configuration (`--speed-codec --stack-check --lean --trunk-tx`, the series through `0045`) measured with and
without `0046-am-air-band-gain-loop`:

| Build | Image | RAM | Free RAM |
|---|---:|---:|---:|
| Series through `0045` | 560,340 | 209,008 | 2,448 |
| With `0046` | 561,068 | 209,016 | 2,440 |
| With `0046`, `CONFIG_AM_RX=n` | 559,768 | 209,008 | 2,448 |

`0046` costs 728 bytes of flash and 8 bytes of RAM (the AM state and gain loop in `fd6818_t`). Zero compiler warnings in all
three; the static stack check passes on the first (least free: idle task, 136 bytes). These figures are not comparable with
the November 16 ones below, which are without `--trunk-tx` and before patches `0036`-`0045`. Not run on a radio.

## November 16, 2026

Releases are built with `--lean`. The bench tools are left out: the P25 capture replay (2,368 B), the P25 self-test,
and the NXDN receive capture `nxdn_snap` (2,116 B; now behind `CONFIG_P25_BENCH` too).

| Image | Switches | Flash | RAM | Free RAM |
|---|---|---:|---:|---:|
| Release (lean), local issue-fix build | `--speed-codec --stack-check --lean` | 550,960 | 206,912 | 4,544 |
| Bench | `--speed-codec --stack-check` | 552,684 | 211,440 | 16 |

The local lean build includes patches 0032-0035 and the P25 control-channel hunting fix. The bench figure is the
earlier build and has not been remeasured with these fixes.

The heap stays at 40.5 KB: the P25 worker still needs its 8 KB stack for live encoding. The static stack check
passes (least free: idle task, 136 bytes) with zero warnings. Lean polls an empty P25 receive queue every 5 ms
instead of 1 ms. That setting was checked on the radio on November 6, but this image has not been run on a radio.

## November 14, 2026

The image with Motorola talk-permit tones, the pink theme, APRS/GPS, and typed DMR messages (patch 0016) uses
**542,456 bytes of flash** and **211,424 of 211,456 bytes of static RAM** (32 bytes unallocated). The RAM figure
already includes the RTOS heap. The composer reuses the message-view scratch buffer; its net static RAM increase
is 8 bytes. The build has zero compiler warnings, and the static stack check passes (least free: idle task, 136
bytes). No on-radio heap measurement or over-the-air message test was performed for this change.

With the MDC-1200 PTT ID and decoding (`integration/current/mdc1200-*.patch`, on upstream fa76000) the image
built by `tools/build_current.py` uses **720,972 bytes of flash** and **205,672 of 211,456 bytes of static RAM** (5,784
bytes unallocated). The same build with `--no-mdc1200` uses 718,404 bytes of flash and 205,616 bytes of RAM, so the
feature costs 2,568 bytes of flash and 56 bytes of RAM (the record fields and editor rows stay in either way). The
feature is off in the patches themselves (`CONFIG_MDC1200` defaults to `n`), but `tools/build_current.py` builds it in
unless given `--no-mdc1200`, so the images built from this repository, the CI's included, carry it. The burst's tones use
the idle APRS transmit buffer, and the decoder's state and sample buffer are the idle APRS demodulator's; static
assertions check their size and alignment. While the decoder runs, the board's 2 KB receive audio ring comes from the
RTOS heap, as on the APRS frequency. The build's stack check passes. Its processor load (about 7,200 14-tap
correlations a second) is not measured on the radio.

The figures and feature comparisons below are the earlier November 13 snapshot, before the added tones, theme,
APRS, and message editor.

One image carries every mode: FM, DMR, P25 conventional, NXDN48 and P25 trunking (control channel, FDMA voice).
The Phase 2 layer is built too. Built with `tools/build_radiofoundry.py`, RadioFoundry pinned at `dfbaf20c`, on
2026-11-13. Not measured on a radio: the figures are the linker's, the static stack check's, and host estimates
calibrated on one on-radio measurement.

## Flash and RAM

The CK803S has 880 KB of flash for the application and 211,456 bytes of RAM. The RAM figure includes the RTOS
heap: 40.5 KB with the P25 worker.

| Image | Switches | Flash | RAM | Free RAM |
|---|---|---:|---:|---:|
| **All modes, receive-only trunking (default)** | P25, NXDN, TRUNK, PHASE2 | 466,576 (51.8 %) | 194,432 | 17,024 |
| **All modes, trunked transmit** | + TRUNK_TX (`--trunk-tx`) | 468,004 (51.9 %) | 195,184 | 16,272 |
| All modes, lean (no bench replay or self-test) | `--lean --trunk-tx` | 466,268 | 192,768 | 18,688 |
| No trunking | `--no-trunk` | 459,588 | 193,176 | 18,280 |
| No trunking, no NXDN | `--no-trunk --no-nxdn` | 445,784 | 190,432 | 21,024 |
| P25 off | `--disable` | 409,064 | 163,352 | 48,104 |

Each mode's cost:

| Mode | Flash | RAM |
|---|---:|---:|
| NXDN48 | 13.8 KB | 2.7 KB |
| P25 trunking, receive | 7.0 KB | 1.3 KB |
| P25 trunking, transmit | 1.4 KB | 0.75 KB |
| Phase 2 | 0 | 0 |

Phase 2 costs nothing because nothing on the radio calls it (no physical layer), so the linker drops it.

Every switch builds on and off with zero warnings.

## Stack (static worst case, all modes with trunked transmit)

| Task | Stack | Deepest | Free |
|---|---:|---:|---:|
| p25-lab (P25 codec, frames, trunking tap) | 8,192 | 5,680 | 2,316 |
| audio (AMBE+2 for DMR and NXDN) | 6,144 | 4,324 | 1,624 |
| radio (radio core, NXDN modem, trunking logic) | 3,072 | 2,140 | 736 |
| ui | 2,016 | 1,548 | 272 |
| start | 2,560 | 1,632 | 732 |
| idle | 576 | 244 | 136 |
| native modem interrupts (separate 2 KB IRQ stack) | 2,048 | 184 | 1,668 |

Every task fits; the least margin is the idle task's 136 bytes, unchanged from the base firmware. The trunking tap's
path in the P25 worker is about 0.5 KB deep, below the encoder's path. The trunking logic runs in the radio task
below its deepest path.

## Real time

`tools/rt_budget.py` (`results/rt-budget.json`) runs each mode's work on the host for one deadline unit and counts
the instructions inside it (callgrind). It converts those to CK803S time at 168 MHz with the one CPU measurement
made on the radio for the same code: the IMBE encoder averaged 13.0 ms per 20 ms frame (0.843 CK803S cycles per
host instruction). As a cross-check, the IMBE decoder comes out at 11.5 ms on average; the radio measured a 12.2 ms
peak.

| Mode | Work per period | Estimate | CPU |
|---|---|---:|---:|
| P25 TX | 9 IMBE encodes + LDU coding per 180 ms | 117.5 ms | 65 % (on the radio: 13.3 ms average, 18.6 ms peak per 20 ms frame) |
| P25 RX | LDU framing and decoding + 9 IMBE decodes per 180 ms | 105.9 ms | 59 % |
| P25 trunk, control channel | a 3-block TSDU (3 Viterbi decodes) + framer + logic per 75 ms | 1.9 ms | 2.5 % |
| P25 trunk, following a call | P25 RX + the control channel tap + logic | 107.1 ms | 59.5 % |
| P25 trunk, talking | P25 TX + logic | 117.5 ms | 65 % |
| NXDN RX | demodulator with every compensation filter, call layer, per 80 ms frame | 0.8 ms | 1.0 % + the AMBE+2 decoder |
| NXDN TX | framing, modulation, the timer interrupt's pulls, per 80 ms frame | 0.6 ms | 0.7 % + the AMBE+2 encoder |
| Phase 2 receive (reference; no PHY) | one burst per 30 ms | 0.13 ms | 0.4 % |

Not covered:
- The stock AMBE+2 vocoder, which DMR and NXDN share. NXDN runs it at DMR's rate, which the firmware already
  sustains.
- Interrupt entry and the RTOS. The on-radio P25 encoder figures include interrupt preemption, because interrupt
  time is charged to the interrupted task.
- The NXDN timer interrupt runs every 2 ms and pulls 16 samples; its work is in the NXDN TX figure.
- The native modem's slot interrupts run every 30 ms in P25.

Modes never run at the same time: a channel is in one mode. The heaviest is P25 TX, measured on the radio at 93 % of
a frame at its peak, with buffering absorbing it. Trunking adds at most 2.5 % to any P25 mode. NXDN adds about 1 %
to DMR's vocoder load.

## Does it fit?

Yes:
- Flash: 52 % used.
- RAM: 16 KB free with every switch on.
- Every stack fits.
- Each mode's real-time load is below one CPU.

No switch needs to be turned off. If the margin has to grow, in this order:

| Order | Change | Gain | Loses |
|---:|---|---|---|
| 1 | `--lean` | 1.7 KB flash, 2.4 KB RAM | The on-radio replay and self-test |
| 2 | `--no-phase2` | No flash or RAM today | Nothing on the radio |
| 3 | Trunked transmit off (the default) | 1.4 KB flash, 0.75 KB RAM | Trunked transmit |
| 4 | `--no-nxdn` | 13.8 KB flash, 2.7 KB RAM | NXDN |

The figures to confirm on the radio are in [hil-checklist.md](hil-checklist.md) step 7.
