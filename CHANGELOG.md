# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-09-03

Pins imud wire protocol **v18** (layout introduced in imud 1.10).

**Compatibility — this is a required update, not an optional one.**
1.2.0 requires **imud ≥ 1.10**; the **1.1.x** line remains correct for imud
1.7–1.9, and **1.0.x** for imud 1.4–1.6. Daemon and sketch have to be
updated together: `ImudParser` rejects any packet whose version word isn't
`IMUD_VERSION`, so a mismatched pairing receives no packets at all — no
error, no misparse, just silence and a `millisSinceLastPacket()` that climbs
forever. There is deliberately no dual-version support, because the CRC
offset differs between v17 and v18: a parser would have to guess the frame
length before it could validate it, on a stream whose framing *is* the
validation.

| Daemon | Wire | ImudClient |
|---|---|---|
| imud 1.4–1.6 | v14 | 1.0.x |
| imud 1.7–1.9 | v17 | 1.1.x |
| imud ≥ 1.10 | v18 | **1.2.x** |

### Added

- `flags_ext` (`uint32`, offset 272) — a **second flag word**, appended
  because `flags` had assigned all sixteen of its bits. It is entirely
  separate from `flags`: bit 0 of one is not bit 0 of the other. One bit is
  defined so far:
  - `IMUD_FLAG_EXT_MAG_ABSENT` (bit 0) — no magnetometer is configured, so
    heading is gravity-referenced only: it starts at zero in whatever
    orientation imud booted in and dead-reckons from the gyro for the life
    of the run. This is **not** the same as `MAG_VALID` and `MAG_UNCAL`
    both being clear, which describes a *fitted* magnetometer that is stale
    or failed and can recover. `MAG_ABSENT` cannot.

  **Test only the bits you recognise, and never compare `flags_ext` for
  equality.** That contract is what lets imud define a new bit here without
  another wire-version bump, so a sketch written against this header keeps
  working against a newer daemon.
- `reserved` (`uint8[8]`, offset 276) — zero on the wire; do not interpret.
  Covered by the CRC.
- `IMUD_FLAG_STATE_RESET` (bit 14 of `flags`) — the MEKF found a non-finite
  value in its own state and reset itself. **Latched** from the reset until
  the filter next converges rather than pulsed for one packet, because at up
  to 500 Hz a momentary bit is invisible to a 1 Hz consumer. While it is set
  the attitude is valid but re-aligning, and `FUSION_CONVERGED` is clear for
  the same span.
- `IMUD_FLAG_MAG_UNCAL` (bit 15 of `flags`, added upstream in imud 1.9.1) —
  heading fused from an **uncalibrated** magnetometer: offset by the
  uncorrected hard iron, but bounded and repeatable, unlike a gyro-only
  heading. Mutually exclusive with `IMUD_FLAG_MAG_VALID`. This needed no
  wire change, only the constant, and was missing from 1.1.x.
- `docs/PROTOCOL.md` gains an "Extended flags" section; `docs/GLOSSARY.md`
  gains plain-language entries for `flags_ext`, `MAG_ABSENT` and
  `MAG_UNCAL`; the README gains a `flags_ext` section with the correct and
  incorrect ways to test it.
- `tools/fake_daemon.py --mag-absent` — emit `EXT_MAG_ABSENT` with every
  mag-derived flag dropped, so the no-compass path can be exercised from a
  sketch without hardware.
- Unit tests for the v18 surface: unknown `flags_ext` bits are ignored
  rather than rejected, `flags_ext` decodes independently of `flags`,
  `MAG_UNCAL` decodes, and every byte of the appended tail is covered by
  the CRC.

### Changed

- `IMUD_VERSION` 17 → **18** and `IMUD_PACKET_SIZE` 276 → **288**.
- `crc32` moves from offset 272 to **284**, and now covers bytes 0–283.
  Derive that range from `offsetof(imud_packet_t, crc32)` rather than
  hardcoding it.
- **Every pre-existing field keeps its offset.** The bump is purely an
  append plus the CRC move, so a decoder's existing offsets stay correct
  and the golden vectors differ from v17's below offset 272 only in the
  version word.
- `IMUD_FLAG_MOTION` (bit 6) stays defined and unused. v18 was the one
  moment it could have been reused safely, and deliberately was not,
  because imud 1.8 published that it would not be.
- Golden vectors in `extras/golden/` regenerated for v18, round-tripped
  through imud's reference Python client as usual. The fuzz seed corpus
  gains `daemon_encoded_v18.hex`, a packet from the daemon's own C encoder
  (mirrored from imud's `test/fuzz/corpus/packet/valid_v18.bin`) — a second
  accept-path seed from a different implementation than the one that
  generates the golden vectors.
- `tools/fake_daemon.py` emits v18 packets.
- No breaking API change: there are no per-field accessors, so consumers
  read `imud.packet().flags_ext` directly.

### Fixed

- The fuzz seed corpus under `test/fuzz/corpus/` was excluded by the `*.hex`
  rule in `.gitignore` and had never been committed, so CI's deterministic
  replay gate was seeded only by the golden vectors and none of the
  structural resync cases. The corpus is now un-ignored, and gains v18-sized
  companions for the two cases whose whole point is the frame boundary:
  a body that fills the buffer exactly and ends in a partial magic, and one
  that stops just short of a full frame.

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
- CI is substantially hardened. New coverage: continuous fuzzing of
  `ImudParser` (60 s per PR, 15 min nightly, seeded from the golden
  vectors); the unit suite re-run under ASan/UBSan with `-Werror`; a
  portability matrix compiling the header across gcc/clang, C++11–20 and
  32/64-bit with `-Wconversion -Werror`; example compilation through the
  Arduino IDE toolchain as well as PlatformIO; Arduino Library Manager
  compliance linting; and `tools/check_repo_integrity.py`, which fails the
  build on the kinds of drift that otherwise produce a green one — an
  example no job compiles, a version disagreement between the two manifests
  and the changelog, test byte-arrays that no longer match
  `extras/golden/*.hex`, a public symbol missing from `keywords.txt`, or a
  broken documentation link. Workflows now set least-privilege
  `permissions`, cancel superseded runs, bound every job with a timeout, and
  pin actions to commit SHAs with Dependabot to update them.
- `IMUD_ASSERT` / `IMUD_ENABLE_ASSERTS` — an internal invariant hook that
  compiles to nothing unless explicitly enabled, so the embedded build is
  byte-for-byte unchanged. Test and fuzz builds enable it. This is what makes
  a `buf_` index bug detectable at all: that array is followed by padding, so
  a small overrun is an intra-object write that AddressSanitizer cannot see.

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
