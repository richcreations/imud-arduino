# imud wire protocol v17 — field reference and parser internals

This document is the deep-dive companion to the [README](../README.md)'s
API reference: the full byte-level layout of the 276-byte packet, the flag
bitmask, the CRC32 definition, and a walkthrough of `ImudParser`'s
stream-resync algorithm. It's aimed at anyone extending `ImudClient`,
debugging a decode issue, or writing a second implementation against the
same wire format.

The struct itself lives in `src/ImudClient.h` and is copied verbatim from
imud's own reference implementation — see the
[wire-sync warning](../README.md#wire-sync-warning) before touching it.

## Framing

- 276 bytes, fixed size, little-endian, naturally aligned (no implicit
  padding — `__attribute__((packed))` in the struct definition is a guard
  against a future field breaking that, not a behavior change today).
- Self-delimiting: a 4-byte magic (`"IMUD"`, `0x494D5544`, little-endian
  bytes `44 55 4D 49` on the wire) plus a trailing CRC32 are enough to find
  and validate frame boundaries in an arbitrary byte stream — no
  length-prefix or delimiter needed.
- TCP carries a back-to-back stream of these frames; UDP carries exactly
  one per datagram.

## Field layout

| Offset | Size | Field | Type | Units / notes |
|---:|---:|---|---|---|
| 0 | 4 | `magic` | `uint32_t` | `IMUD_MAGIC` = `0x494D5544` |
| 4 | 2 | `version` | `uint16_t` | `IMUD_VERSION` = 17; reject anything else |
| 6 | 2 | `flags` | `uint16_t` | `IMUD_FLAG_*` bitmask, see below |
| 8 | 8 | `ts_wall_ns` | `uint64_t` | `CLOCK_REALTIME`, nanoseconds |
| 16 | 8 | `ts_tai_ns` | `uint64_t` | `CLOCK_TAI`, nanoseconds |
| 24 | 4 | `ts_chip_ticks` | `uint32_t` | IMU hardware counter |
| 28 | 4 | `anchor_gen` | `uint32_t` | increments on wall-clock re-anchor |
| 32 | 4×3 | `accel_x/y/z` | `float` | m/s², calibrated |
| 44 | 4×3 | `accel_raw_x/y/z` | `float` | m/s², pre-calibration |
| 56 | 4×3 | `gyro_x/y/z` | `float` | rad/s, bias-corrected |
| 68 | 4×3 | `gyro_raw_x/y/z` | `float` | rad/s, before bias correction |
| 80 | 4×3 | `mag_x/y/z` | `float` | µT, calibrated |
| 92 | 4×3 | `mag_raw_x/y/z` | `float` | µT, pre-calibration |
| 104 | 4×4 | `quat_w/x/y/z` | `float` | unit quaternion, body→NED |
| 120 | 4 | `pitch` | `float` | rad, NED (+bow up) |
| 124 | 4 | `roll` | `float` | rad, NED (+starboard up) |
| 128 | 4 | `yaw` | `float` | rad, NED magnetic |
| 132 | 4 | `heading_deg` | `float` | 0–360°, **magnetic** |
| 136 | 4 | `rate_of_turn` | `float` | deg/min, + = turning right |
| 140 | 4 | `temp_c` | `float` | IMU die temperature, °C |
| 144 | 4×9 | `cov[9]` | `float` | 3×3 attitude error covariance, row-major (rad²) |
| 180 | 4 | `imu_seq` | `uint32_t` | monotonic sample counter (daemon-side, not per-packet-received) |
| 184 | 4 | `declination_deg` | `float` | °E+; `0.0` unless `DECLINATION_VALID` |
| 188 | 4 | `heave_m` | `float` | m, +up; `0.0` unless `HEAVE_VALID` |
| 192 | 4×3 | `gyro_bias_x/y/z` | `float` | rad/s, estimated gyro bias |
| 204 | 4×3 | `gyro_bias_var_x/y/z` | `float` | (rad/s)², gyro-bias variance |
| 216 | 4 | `heave_rate` | `float` | m/s, +up; `0.0` unless `HEAVE_VALID` |
| 220 | 4 | `accel_quiescence` | `float` | EMA of (\|a\|/g − 1)² |
| 224 | 4 | `wave_height_m` | `float` | significant wave height Hs, m; `0.0` unless `WAVE_VALID` |
| 228 | 4 | `wave_period_s` | `float` | mean zero-crossing period Tz, s |
| 232 | 4 | `roll_period_s` | `float` | vessel roll period, s; `0.0` = not rolling |
| 236 | 4 | `roll_amplitude` | `float` | significant single amplitude 2σ(roll), rad |
| 240 | 4 | `pitch_period_s` | `float` | vessel pitch period, s |
| 244 | 4 | `pitch_amplitude` | `float` | significant single amplitude 2σ(pitch), rad |
| 248 | 4 | `mag_anomaly` | `float` | EMA of \|\|B\|−\|B_ref\|\|/\|B_ref\| (unitless) |
| 252 | 4 | `mag_residual` | `float` | EMA of \|heading innovation\|, rad — compass health |
| 256 | 4 | `innov_weight` | `float` | EMA of the Huber weight √(γ/d²) applied to accepted updates; `1.0` = never capped, → `0.33` = sustained capping at the reject boundary |
| 260 | 4 | `innov_reject` | `float` | EMA of the reject indicator: fraction of updates discarded by the gross-outlier gate; `0.0` = nothing rejected |
| 264 | 4 | `nis_accel` | `float` | rolling normalised innovation squared for the accelerometer update, d²/2 |
| 268 | 4 | `nis_mag` | `float` | same for the magnetometer update, d²/2 (3-D) or d²/1 (yaw-only) |
| 272 | 4 | `crc32` | `uint32_t` | IEEE 802.3 CRC32 of bytes 0–271 |

Total: 276 bytes.

### Reading the gate-health and NIS fields

The layout above tells you where these four live, but not how to interpret
them, and the distinction matters:

- `nis_accel` / `nis_mag` are normalised by effective degrees of freedom,
  so **`1.0` means the filter's covariance correctly predicts its own
  innovation spread**. Above 1 = over-confident; well below 1 = carrying
  more uncertainty than it needs. Where `innov_weight`/`innov_reject`
  report how hard the robustness machinery is working, these report
  whether the noise model itself is right.
- They are accumulated **before** the Huber cap and **include**
  gate-rejected updates, so unlike `innov_weight` — which saturates once
  the cap engages — they keep climbing as the model gets worse.
- `nis_mag` reads **lower in 3-D magnetometer mode by design** when the
  daemon's `mekf_mag_dip_sigma_deg` is non-zero: the dip channel is
  deliberately trusted less. Do not compare `nis_mag` across magnetometer
  modes, and do not treat a low value as a fault.
- The time constant is ~30 s, so all four are slow-moving. Treat them as
  health indicators, not per-packet signals — a single packet's value
  says nothing on its own.

## Flags (`flags`, offset 6)

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `IMUD_FLAG_MAG_VALID` | mag healthy and calibrated |
| 1 | `IMUD_FLAG_MAG_SET_RESET` | SET pulse within last read |
| 2 | `IMUD_FLAG_FUSION_CONVERGED` | filter covariance settled — don't trust attitude before this |
| 3 | `IMUD_FLAG_ACCEL_CAL` | accel calibration applied |
| 4 | `IMUD_FLAG_GYRO_CAL` | gyro bias applied |
| 5 | `IMUD_FLAG_MAG_CAL` | mag hard/soft-iron applied |
| 6 | `IMUD_FLAG_MOTION` | reserved — never set in v17 |
| 7 | `IMUD_FLAG_FIFO_OVERFLOW` | sample gap (FIFO overflow) |
| 8 | `IMUD_FLAG_STARTUP` | gyro bias estimation in progress |
| 9 | `IMUD_FLAG_SHUTDOWN` | final packet before clean daemon exit |
| 10 | `IMUD_FLAG_DECLINATION_VALID` | declination known — gates `declination_deg` and `trueHeading()` |
| 11 | `IMUD_FLAG_HEAVE_VALID` | heave estimator settled — gates `heave_m`/`heave_rate` |
| 12 | `IMUD_FLAG_WAVE_VALID` | sea-state stats settled — gates `wave_*`/`roll_*`/`pitch_*` (period/amplitude) |
| 13 | `IMUD_FLAG_ENGINE_ON` | engine-vibration detected |

## Validation order (normative)

Applied in this exact order — get it right and a malformed or truncated
packet is rejected as cheaply as possible, without ever touching
uninitialized or attacker-controlled memory beyond the buffer bounds:

1. **Size** — exactly 276 bytes.
2. **Magic** — `magic == IMUD_MAGIC` (wire bytes `44 55 4D 49`).
3. **Version** — `version == IMUD_VERSION` (17); reject anything else.
4. **CRC32** — computed CRC32 of bytes 0..271 equals the stored `crc32` at
   offset 272.

Anything that fails any step is discarded silently and counted (never
logged per-packet — a malicious or malfunctioning peer flooding invalid
frames shouldn't also flood your log).

## CRC32

IEEE 802.3 / zlib polynomial (`0xEDB88320`), computed bitwise with no
lookup table:

```cpp
uint32_t imud_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return crc ^ 0xFFFFFFFFu;
}
```

276 bytes at 240 MHz is on the order of microseconds, so a table isn't
worth the ~1 KB of flash — this is deliberate, not an oversight; don't
"optimize" it into a CRC library dependency.

## Stream resync algorithm (`ImudParser::feed()`)

The TCP transport contract guarantees frames arrive whole and aligned from
the first byte of a fresh connection (§ Server contract in the README), so
in practice resync only fires after real corruption or an unlucky
mid-stream reconnect. `feed()` treats that as the exception, not the rule,
but the algorithm has to be correct for *any* byte stream, not just a
well-behaved TCP socket — that's what makes it safe to also use as the
receive path for, say, a serial bridge or a captured-frame file.

The core loop:

1. Append each incoming byte to a 276-byte accumulation buffer.
2. When the buffer fills (276 bytes), validate it (the four steps above).
   - **Valid** → copy it out as the newest packet, empty the buffer,
     continue.
   - **Invalid** → resync (next section), which leaves the buffer holding
     however many bytes look like they could still be the start of a real
     frame, then continue accumulating from there.

Resync never discards the whole buffer as its default action — a valid
frame may start partway through what's currently buffered, and throwing
all of it away would risk cutting that frame in half:

1. Drop exactly one byte from the front (`memmove` the rest down by one).
2. Scan the remaining bytes for a full 4-byte magic match. If found,
   discard everything before it (another `memmove`) and stop — the buffer
   now starts at a plausible frame boundary.
3. If no full match exists, check whether the *last* 1–3 bytes of the
   buffer are a prefix of the magic sequence (e.g. the buffer ends in
   `44 55 4D`, one byte short of a complete magic). If so, keep just that
   tail — the next `feed()` call's bytes can complete the match. This
   matters because `feed()` accepts arbitrary chunking, including one byte
   at a time; without it, a magic sequence that happens to straddle a
   resync point could be missed.
4. Otherwise, nothing in the buffer is salvageable — empty it.

Each call to this routine increments `resyncs()`, independent of
`crcErrors()` (which counts the validation failure that triggered it).

### Worked example: `extras/golden/resync_stream.hex`

This 573-byte vector is built specifically to exercise the false-lock
case: 21 bytes of garbage containing a **decoy** magic sequence at offset
7 (followed by a plausible version, `11 00` = 17, but garbage after that),
then two genuinely valid frames back to back (`imu_seq` 1000 and 1001).

Fed through `feed()` (whole, one byte at a time, or in any other
chunking), the trace looks like:

1. The buffer fills with stream bytes `[0..275]` — starts with garbage, so
   magic check fails at byte 0. Resync drops 1 byte, then finds the decoy
   magic (now at buffer offset 6) and discards everything before it.
2. Accumulation continues; the buffer eventually holds the decoy magic
   plus its plausible-looking version field, followed by more garbage.
   Magic and version both pass — a **false lock** — but the CRC32 doesn't
   match anything real, so validation fails at the CRC step. Resync fires
   again: drop 1 byte, scan forward, and this time find frame A's *real*
   magic sequence.
3. From there, accumulation runs cleanly to the end of frame A (`imu_seq`
   1000), validates, and is emitted. Frame B (`imu_seq` 1001) follows
   immediately and validates on the first try — no resync needed.

Net result: exactly 2 valid packets, in order, `resyncs() >= 1` — verified
in `test/test_parser/test_parser.cpp` both fed whole and fed one byte at a
time (chunking must not change the outcome, since a real TCP stack can
deliver bytes in any grouping).

## Timing notes

- TCP default rate is 100 Hz (10 ms period); UDP high-rate default is up
  to 500 Hz. Neither is a hard guarantee to the client — see
  [Server contract](../README.md#server-contract-what-the-client-tolerates)
  in the README for what gets skipped, and when.
- `ts_wall_ns`/`ts_tai_ns` are the daemon's clocks at sample time, not
  network receipt time — don't use them to measure link latency without
  also accounting for clock skew between the daemon host and your board.
- `imu_seq` is the right counter for detecting *sample* gaps (skipped
  daemon ticks or dropped UDP datagrams); it says nothing about wall-clock
  timing on its own.
