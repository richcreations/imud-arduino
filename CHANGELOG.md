# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-07-26

Pins imud wire protocol **v17** (layout introduced in imud 1.7).

**Compatibility — this is a required update, not an optional one.**
1.1.0 requires **imud ≥ 1.7**; the **1.0.x** line remains correct for imud
1.4–1.6. The two are not interchangeable: `ImudParser` rejects any packet
whose version word isn't `IMUD_VERSION`, so a mismatched pairing receives
no packets at all — no error, just silence and a `millisSinceLastPacket()`
that climbs forever. There is deliberately no dual-version support, because
the CRC offset differs between v14 and v17: a parser would have to guess
the frame length before it could validate it, on a stream whose framing
*is* the validation.

### Added

- Four `float32` fields appended to `imud_packet_t` after `mag_residual`,
  reporting MEKF update-gate health and covariance consistency. All four
  are EMAs with a ~30 s time constant — health indicators, not per-packet
  signals:
  - `innov_weight` (offset 256) — EMA of the Huber weight √(γ/d²) applied
    to accepted updates; `1.0` = never capped, → `0.33` = sustained
    capping at the reject boundary.
  - `innov_reject` (offset 260) — EMA of the reject indicator: the
    fraction of updates discarded by the gross-outlier gate; `0.0` =
    nothing rejected.
  - `nis_accel` (offset 264) — rolling normalised innovation squared for
    the accelerometer update, d²/2. `1.0` means the filter's covariance
    correctly predicts its own innovation spread.
  - `nis_mag` (offset 268) — the same for the magnetometer update, d²/2
    (3-D) or d²/1 (yaw-only). Reads lower in 3-D mode by design when the
    daemon's `mekf_mag_dip_sigma_deg` is non-zero; don't compare it across
    magnetometer modes.
- `docs/PROTOCOL.md` gains a "Reading the gate-health and NIS fields"
  section covering the consumer semantics the byte layout doesn't imply.
- `imud_rad_to_deg()` and `IMUD_RAD_TO_DEG` — `pitch`/`roll`/`yaw` are
  radians while `heading_deg` is degrees, a mismatch that previously left
  every sketch open-coding the same magic constant.
- `examples/HelloAttitude` — a minimal first sketch: connect, then print
  heading, pitch and roll. No reconnect, staleness, or counters; those stay
  in `TcpBasic`, which now points newcomers here first.
- Documentation aimed at people new to the library, none of which existed
  before: `docs/GETTING-STARTED.md` (zero to a first reading, no IMU
  hardware required), `docs/TROUBLESHOOTING.md` (organized by symptom), and
  `docs/GLOSSARY.md` (NED, declination, heave, NIS and the rest, in plain
  language). The README gains a "What's in a packet" field table with units
  and flag gating.

### Changed

- `IMUD_VERSION` 14 → **17** and `IMUD_PACKET_SIZE` 260 → **276**.
- `crc32` moves from offset 256 to **272**, and now covers bytes 0–271.
- Golden vectors in `extras/golden/` regenerated for v17. They are now
  produced by a generator that round-trips every vector through imud's own
  reference Python client before writing, so they cannot drift from the
  daemon.
- `tools/fake_daemon.py` emits v17 packets, with the four new fields set to
  the healthy case rather than zero.
- No breaking API change: there are no per-field accessors, so consumers
  read `imud.packet().nis_accel` and friends directly. The only addition to
  the API surface is the `imud_rad_to_deg()` helper above.
- `examples/TcpBasic` now prints yaw alongside heading/roll/pitch, labels
  every angle with its unit, and explains what the `crc_err`/`resyncs`
  counters mean. `examples/UdpListen` now prints full attitude rather than
  heading alone, and says "no packets yet" instead of showing a
  zero-initialized `hdg=0.0` that looks like real data.
- `CONTRIBUTING.md` now states the actual versioning policy: a wire bump is
  a **minor** release, with major reserved for large feature or structural
  changes. It previously called for a major bump on every wire change.

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
