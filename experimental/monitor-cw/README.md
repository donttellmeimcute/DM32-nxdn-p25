# Monitor Scan and CW: experimental core (not a firmware feature yet)

This directory is a portable prototype. It is not compiled by the production
firmware build, does not touch RF registers, and cannot transmit. Do not merge
or release as a working radio feature until the adapter, UI and target tests
are complete.

## Host test

The firmware build driver now runs the native regression before fetching or
building the target. Run just that check with
`python3 tools/build_current.py --monitor-cw-tests-only` (set `HOSTCC` if needed).
This does not enable the features in a radio image.

`monitor_session.h` defines the radio-task adapter contract. It acquires RX
ownership before tuning and keeps the TX interlock asserted on a restoration
failure until recovery succeeds. Mock tests exercise these paths. The actual
radio task must implement the callbacks and enforce the interlock at every TX
producer; the adapter by itself is not a physical TX interlock.


```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Os -fsanitize=undefined test.c -o /tmp/dm32-mc-test
/tmp/dm32-mc-test
```

The scan state machine supports an inclusive frequency raster, a band-policy
callback, adjustable absolute RSSI threshold, settling/dwell/verification,
pause, next/resume and evidence-based mode filtering. Initialize the state
to zero before the first start; keep its tune generation across restarts.
The adapter must attach the generation to actual fresh hardware samples.
Combined discovery can hold an unidentified strong carrier, explicitly
labeled UNKNOWN. No carrier alone is FM, digital or CW proof. The 256-step
hole limit is intentional; the production adapter should iterate actual board
band intervals to cover wide discontinuous ranges. A held result exposes the
frequency and peak RSSI, but no codeplug writes are implemented. Resume currently
advances to the next raster frequency.

The Morse codec maps ASCII letters and digits, converts 5–40 WPM to dot
duration, and decodes fixed-speed mark durations. The caller must detect mark
edges and letter/word gaps; it is not a radio audio demodulator or a TX keyer.

## Target integration gates

- Existing `core/radio/scan.c`, `radio_sm.c` and
  `integration/current/vfo-scan-limits-and-digital-sweep.patch` implement
  VFO scans; reuse the radio task's ownership rather than tuning from UI.
  Use board_band_count()/board_in_band(hz,false) and verified FM-compatible
  ranges; don't pass airband/broadcast/unsupported extended bands to FM or
  digital probing just because the synthesizer can tune them. Do not change
  the active channel until the user accepts the result.
- Monitor scanning must inhibit *all* TX paths before the first retune,
  including PTT, trunking registration, APRS, emergency, hotspot and queued
  TX. Stop/restore the saved radio state on exit or retune failure. PTT
  during this tool must not transmit. Saving must be explicit and RX-only.
  Add host tests for each of these behaviors, with zero HAL TX calls.
- RSSI comes from FD6818 register 0x67, but RSSI measures signal strength,
  not distance or modulation. Use stable samples after settling; DMR
  needs sufficient dwell for TDMA bursts. At a candidate frequency,
  probe only decoders compiled into the selected build and require
  validated frame/sync evidence. Avoid false identification from noise,
  images, adjacent channels and overload. No documented instantaneous
  frequency-counter/Close Call facility has been established.
- RadioFoundry's pinned hardware document records the FD6818 as a
  BK4819-class *hypothesis*, not a verified register-compatible chip.
  Do not copy BK4819 SSB/BFO register values blindly. FM/AM demodulated
  audio is not complex I/Q. Establish an actual BFO or accessible coherent
  IF/baseband path for audible A1A CW; test with an offset keyed carrier.
  An RSSI-keyed Morse experiment is a separate envelope detector and
  does not establish A1A demodulation or a narrow receive bandwidth.
- Existing SFR/hotspot Morse ID uses an FM carrier with a keyed audio
  tone (see upstream docs/spec/sfr.md), not proven keyed RF. Reuse timing
  ideas only. For true CW TX a separate board keying path must obey
  all TX policy checks, PA sequencing, PLL lock, power and fail-safe
  release; characterize ramp/occupied bandwidth/key clicks using a
  dummy load and SDR/service monitor. No RF TX enabled by this prototype.
- Add optional CONFIG_MONITOR_SCAN and CONFIG_CW, explicit Custom Modes
  build plumbing and feature flags, UI with threshold/range/filter,
  pause/resume/save, mode evidence and CW settings. Only advertise modes
  compiled in. Do not alter codeplug ABI until migration is tested.
- Build the pinned patched source via tools/build_current.py, compare
  full/multimode/dmr-fm and custom presets, run host/UI regressions,
  stack-check, map/size and on-device tests before considering release.
  The existing RAM defaults disable SFR and HTTP; preset-specific
  stack sizing means host sizeof alone is not a target memory budget.
