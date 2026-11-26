<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 lyra XE1POC -->

# D-STAR DV on the DM-32UV

Build with `python3 tools/build_current.py`. Pass `--no-dstar` to build without
D-STAR. Use the radio name in Settings → My radio as the station's callsign
(up to eight letters and digits). In the VFO, choose Quick → Mode. To save a
memory, open Channels → Edit channel → Mode, select D-STAR on a supported
frequency and hold OK to save. The web Codeplug Editor shows D-STAR when
the radio reports the feature. The channel tag stores the mode and the
receive-only setting alongside the channel's standard frequencies.

D-STAR DV uses a nominal 6.25 kHz channel raster. The VFO's Auto step is
6.25 kHz in D-STAR; explicit step settings remain available. Quick displays
`6.25k` as a fixed nominal channel designation, not an RF filter setting.
The DM-32UV's supported narrow FM digital RF path uses the 12.5 kHz filter
profile. Do not substitute an unverified FD6818 register value or change
deviation to imitate a narrower receive filter. Occupied bandwidth and actual
filter response still require measurements on hardware.

The incoming source appears on the home screen after a valid header. The VFO
and channels with both repeater fields blank use direct routing. With URCALL
blank, the radio sends `CQCQCQ`.

## External hotspot

Open Channels → Edit channel on the radio and select D-STAR in Mode. Edit
URCALL, RPT1 and RPT2 with OK, then hold OK to save the memory. The web CPS
can edit the same fields. For simplex hotspots, give the memory the same RX
and TX frequency. Copy `RPT1` and
`RPT2` from the D-Star section of the [WPSD dashboard](https://manual.wpsd.radio/modes/dstar/).
For example, use `N0CALL B` for `RPT1` when the node uses module B and `N0CALL G`
for `RPT2`. The editor pads the callsigns so the module is in the eighth column.
Use `CQCQCQ` for voice or enter `E` in URCALL for the node's echo command.
The radio sends the repeater flag even on a zero-offset memory.

The host regression decodes the routed header from the transmitted symbol
stream, receives a reply with audio, and checks legacy direct channels. Check
both directions with the physical hotspot before relying on the path.

The default DM-32UV build omits the built-in HTTP status server and the DMR
single-frequency repeater (SFR) to leave more main RAM free. CPS, APRS
transmit and receive, iGate, KISS, DMR Tier III, and the custom startup image
are available in this build.

The radio header, sync, and slow-data idle follow JARL's
[D-STAR specification](https://www.jarl.org/English/7_dstar/STD7_0_E.pdf),
sections 4.1 and 6 and appendices 1–2. Microphone and speaker blocks go through
a fixed-point first-generation AMBE voice implementation. Its decoder uses the
ISC-licensed tables from mbelib, with attribution in the table header. The
encoder estimates the harmonics and quantizes them independently. D-STAR DV
uses the software implementation and the idle vocoder's RAM window. DMR and
NXDN still require the compatible factory AMBE+2 codec. Preserve it with the
RadioFoundry serial stub if already installed or merge it locally from a
supported manufacturer firmware file when using the factory loader.

The channel regression checks both protected words and both uncoded words
against the original ISC-licensed DSD interleave schedule. A software bench
also plays a modeled discriminator WAV into DSD-FME with `-fd` to check the
callsigns and voice with an independent receiver.

The receiver uses the 8 kHz discriminator stream and 4800-symbol/s binary
profile. Its sync fit uses the interpolated discriminator samples directly
and allows for the rounded transitions of a filtered Gaussian signal.
The DM-32UV transmits 4800 binary symbols/s through the HR_C7000's
RRC pulse path with a nominal outer deviation of 1200 Hz. The provider marks
this shape as approximate. Validate deviation, occupied bandwidth, RX/TX with
another D-STAR radio, and the 20 ms audio cadence on a cabled bench before RF
use. The voice codec has a 20 ms frame period. Occasional late frames can recover
on the next blocks. Sustained overload stops the current TX or mutes the current
RX once processing falls two frames behind. A fresh call can start normally.
The host tests inject short delays and sustained overload. They do not measure
the physical transmitter or MCU timing.

From the repository root after preparing the RadioFoundry tree:

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc/dstar -Ibuild/current/core \
    src/dstar/dv_frame.c src/dstar/dv_stream.c src/dstar/ambe_wire.c \
    build/current/core/dmr/ambe/ambeFec.c tests/test_dstar_stream.c \
    -o /tmp/test-dm32-dstar-stream
/tmp/test-dm32-dstar-stream

# Exercise acquisition with a modeled GMSK discriminator signal.
cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Isrc/dstar -Ibuild/current/core -Ibuild/current/hal \
    src/dstar/dv_frame.c src/dstar/dv_stream.c src/dstar/dv_engine.c \
    build/current/core/air/air_softmodem.c tests/test_dstar_gmsk.c \
    -o /tmp/test-dm32-dstar-gmsk
/tmp/test-dm32-dstar-gmsk
```
