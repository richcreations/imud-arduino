# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-07-19

Initial release. Pins imud wire protocol **v14** (layout introduced in
imud 1.4, unchanged through 1.6).

### Added

- `ImudParser` — pure C++ decoder for imud's 260-byte wire v14 packet:
  stream reassembly with magic-rescan resync (TCP path) and single-datagram
  validation (UDP path), plus `packetsReceived()`/`crcErrors()`/`resyncs()`
  counters.
- `ImudClient` — Arduino wrapper around the abstract `Client`/`UDP`
  transports: `beginTCP()`/`beginUDP()`, non-blocking `poll()`,
  throttled auto-reconnect on TCP, `millisSinceLastPacket()` staleness
  tracking, and `daemonShutdown()` detection.
- `imud_true_heading()` helper, ported verbatim from imud's reference
  implementation, hardened against Inf/NaN/out-of-range wire data.
- Examples: `TcpBasic` (WiFi + TCP, reconnect, staleness) and `UdpListen`
  (multicast join, high-rate receive, achieved-rate reporting). Both
  branch on `ESP32`/`ESP8266`/`ARDUINO_ARCH_RP2040` to pick the right WiFi
  header and multicast-join signature per core.
- Native unit test suite (`pio test -e native`, Unity) covering every
  golden vector in `extras/golden/`: exact field-value decode, byte-at-a-
  time and chunked feeding, bad-CRC rejection, version-mismatch rejection,
  and the mid-stream resync scenario.
- CI (`.github/workflows/ci.yml`): native tests on every push/PR, plus
  example compilation for `esp32dev`, `esp32-s3-devkitc-1`, and
  `esp32-c3-devkitm-1` (best-effort `d1_mini` and `rpipicow`).
- CodeQL code scanning (`.github/workflows/codeql.yml`): advanced-setup
  workflow covering C/C++ (`ImudParser`, built via
  `pio test -e native --without-testing` so CodeQL can trace real compiler
  invocations for a header-only library) and Python (`tools/fake_daemon.py`),
  on push/PR and weekly.
- Documentation: this README's quick start and API reference, plus
  `docs/PROTOCOL.md` for the full field-by-field wire layout and a
  walkthrough of the resync algorithm.
