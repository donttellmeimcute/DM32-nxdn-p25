# Monitor Scan and Morse core tests

The portable scan state machine accepts a frequency raster, a board-band
policy, a configurable RSSI threshold and decoder evidence. The session
adapter takes RX ownership before tuning and keeps the TX lock asserted
until the saved receive state has been restored. The DM32 runtime supplies
the board calls through the RadioFoundry patch stack.

Build the optional features for the DM-32UV with:

```sh
python3 tools/build_current.py --with monitor-scan --with cw
```

The firmware build runs the portable regression with the native compiler.
Run it independently with `python3 tools/build_current.py --monitor-cw-tests-only`
or use the following command in this directory.

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Os -fsanitize=undefined test.c -o /tmp/dm32-mc-test
/tmp/dm32-mc-test
```

The patched RadioFoundry tree also contains tests for the real adapter and
the radio UI. The standalone portable test does not exercise board GPIO or
RF. See the [operating guide and RF limits](../../docs/monitor-scan-cw.md).
