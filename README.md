# ImudClient

Arduino client library for [imud](https://github.com/richcreations/imud) — an
IMU daemon for marine and robotics navigation that fuses gyro/accel/
magnetometer data with a Kalman filter and publishes attitude (heading,
roll, pitch, quaternion, rate of turn, heave, sea state…) over TCP or UDP.

`ImudClient` receives, validates, and decodes imud's binary packets on
ESP32-class boards — cockpit displays, NMEA gauges, autopilot remotes — so a
sketch can do:

```cpp
if (imud.poll())
    display.show(imud.packet().heading_deg);
```

- **Both transports, same packet.** TCP (`:10112`, lossless, framed, up to
  8 clients) or UDP (`:10111`, unicast/broadcast/multicast, up to 500 Hz).
- **Portable core.** The parser depends only on the C++ standard library and
  is unit-tested on a host PC — no Arduino, no mocks. The Arduino wrapper
  uses only the abstract `Client`/`UDP` base classes, so it works with
  `WiFiClient`/`WiFiUDP`, `EthernetClient`/`EthernetUDP`, or anything else
  that implements those interfaces.
- **No heap, no `String`, no exceptions.** Fixed buffers only (~600 B RAM).
- **Header-only.** Drop in `src/ImudClient.h` and `#include <ImudClient.h>`.

Primary target is **ESP32** (Arduino IDE and PlatformIO); the library also
compiles for ESP8266, RP2040 (Pico W), and Ethernet-shield boards.

> **New to this library? Start with
> [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md).** It walks you from an
> empty sketch to live heading, pitch and roll on your Serial Monitor —
> without an IMU, without wiring anything, using the test server bundled in
> `tools/`. The rest of this README is reference material.
>
> Unfamiliar terms? [docs/GLOSSARY.md](docs/GLOSSARY.md).
> Something not working? [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

---

## Table of contents

- [Getting started](docs/GETTING-STARTED.md) — beginner walkthrough
- [Installation](#installation)
- [Quick start](#quick-start)
  - [TCP](#tcp)
  - [UDP](#udp)
  - [UDP multicast](#udp-multicast)
- [What's in a packet](#whats-in-a-packet)
- [API reference](#api-reference)
- [Protocol semantics](#protocol-semantics)
- [Server contract (what the client tolerates)](#server-contract-what-the-client-tolerates)
- [Enabling the real daemon's outputs](#enabling-the-real-daemons-outputs)
- [Testing](#testing)
- [Troubleshooting](docs/TROUBLESHOOTING.md) — by symptom
- [Glossary](docs/GLOSSARY.md) — NED, declination, heave, NIS…
- [Wire-sync warning](#wire-sync-warning)
- [Repository layout](#repository-layout)
- [Contributing](#contributing)
- [License](#license)

## Installation

### Arduino IDE

1. **Tools → Manage Libraries…** (or Sketch → Include Library → Manage
   Libraries…).
2. Search for **ImudClient** and click Install.
3. `#include <ImudClient.h>` in your sketch.

The examples then appear under **File → Examples → ImudClient**; start with
`HelloAttitude`.

<details>
<summary>Installing manually instead (unreleased versions, or a fork)</summary>

1. Download this repository as a ZIP (or `git clone` it into your Arduino
   `libraries/` folder).
2. Sketch → Include Library → Add .ZIP Library… (or just restart the IDE
   if you cloned directly into `libraries/`).

</details>

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
    https://github.com/richcreations/imud-arduino.git
```

## Quick start

### TCP

Lossless, framed, up to 8 concurrent clients — the right choice for a
display that needs every attitude update in order.

```cpp
#include <WiFi.h>
#include <ImudClient.h>

ImudClient imud;
WiFiClient net;

void setup() {
    Serial.begin(115200);          // set the Serial Monitor to 115200 too
    WiFi.begin("ssid", "password");
    while (WiFi.status() != WL_CONNECTED) delay(100);

    imud.beginTCP(net, "192.168.1.50", 10112);  // stores host/port, connects
}

void loop() {
    if (imud.poll()) {                    // true = at least one NEW valid packet
        const imud_packet_t &p = imud.packet();

        // heading_deg is already degrees (and magnetic);
        // pitch/roll/yaw are radians, hence imud_rad_to_deg().
        Serial.printf("heading %.1f  pitch %.1f  roll %.1f\n",
                      p.heading_deg,
                      imud_rad_to_deg(p.pitch),
                      imud_rad_to_deg(p.roll));
    }
}
```

`beginTCP()`'s initial connect can fail (daemon not up yet, wrong IP) —
that's fine, auto-reconnect takes over. See
[examples/TcpBasic](examples/TcpBasic/TcpBasic.ino) for a complete sketch
with staleness detection, reconnect handling, and daemon-shutdown reporting.

### UDP

Higher rate (up to 500 Hz), unicast or broadcast — bind is all that's
needed:

```cpp
#include <WiFi.h>
#include <ImudClient.h>

ImudClient imud;
WiFiUDP udp;

void setup() {
    WiFi.begin("ssid", "password");
    while (WiFi.status() != WL_CONNECTED) delay(100);

    imud.beginUDP(udp, 10111);   // calls udp.begin(10111) internally
}

void loop() {
    if (imud.poll()) {
        const imud_packet_t &p = imud.packet();
        Serial.println(p.heading_deg);
    }
}
```

### UDP multicast

imud's default high-rate destination is multicast `239.255.0.1`. The
abstract Arduino `UDP` class has no portable multicast-join method, so the
sketch joins first with the concrete transport's API, then hands the
already-bound socket to `ImudClient`:

```cpp
WiFiUDP udp;
udp.beginMulticast(IPAddress(239, 255, 0, 1), 10111);   // ESP32 API
imud.beginUDP(udp, 10111, /*alreadyBound=*/true);
```

See [examples/UdpListen](examples/UdpListen/UdpListen.ino) for a complete
sketch that also reports the achieved packet rate.

## What's in a packet

Every packet carries the same full set of fields. Read them straight off the
struct — `imud.packet().heading_deg` — there are no per-field accessors.

**Watch the units:** `heading_deg` is degrees, but `pitch`, `roll` and `yaw`
are **radians**. Convert with `imud_rad_to_deg()`.

Fields marked "gated by" are only meaningful once that flag is set; before
then they read `0.0`. Terms are defined in
[docs/GLOSSARY.md](docs/GLOSSARY.md).

### Attitude — what most sketches want

| Field | Meaning | Units | Gated by |
|---|---|---|---|
| `heading_deg` | magnetic heading | degrees, 0–360 | — |
| `trueHeading()` | true (geographic) heading; `-1.0f` if unknown | degrees, 0–360 | `DECLINATION_VALID` |
| `pitch` | bow up (+) / down | **radians** | — |
| `roll` | starboard up (+) / down | **radians** | — |
| `yaw` | rotation about vertical, magnetic | **radians** | — |
| `quat_w/x/y/z` | same orientation as a quaternion, body→NED | unit quaternion | — |
| `rate_of_turn` | turn rate, + = turning right | **degrees/minute** | — |
| `declination_deg` | local magnetic variation, + = east | degrees | `DECLINATION_VALID` |

### Motion and sea state

| Field | Meaning | Units | Gated by |
|---|---|---|---|
| `heave_m` | vertical displacement, + up | metres | `HEAVE_VALID` |
| `heave_rate` | vertical velocity, + up | m/s | `HEAVE_VALID` |
| `wave_height_m` | significant wave height (Hs) | metres | `WAVE_VALID` |
| `wave_period_s` | mean zero-crossing wave period | seconds | `WAVE_VALID` |
| `roll_period_s` / `pitch_period_s` | vessel roll / pitch period; `0.0` = not rolling | seconds | `WAVE_VALID` |
| `roll_amplitude` / `pitch_amplitude` | significant single amplitude (2σ) | radians | `WAVE_VALID` |

### Raw and calibrated sensors

| Field | Meaning | Units |
|---|---|---|
| `accel_x/y/z` | acceleration, calibrated, [NED](docs/GLOSSARY.md#ned) | m/s² |
| `gyro_x/y/z` | angular rate, bias-corrected | rad/s |
| `mag_x/y/z` | magnetic field, calibrated | µT |
| `accel_raw_*` / `gyro_raw_*` / `mag_raw_*` | the same before calibration | as above |
| `gyro_bias_x/y/z` | estimated gyro bias | rad/s |
| `temp_c` | IMU die temperature | °C |

### Health and diagnostics

| Field | Meaning | Good value |
|---|---|---|
| `flags` | `IMUD_FLAG_*` bitmask — see below | — |
| `imu_seq` | monotonic sample counter; gaps = dropped samples | increments by 1 |
| `cov[9]` | 3×3 attitude error covariance, row-major | smaller = more confident |
| `mag_anomaly` | nearby-metal indicator | near `0.0` |
| `mag_residual` | compass disagreement with the filter | near `0.0` |
| `innov_weight` | how much the outlier cap is engaging | `1.0` |
| `innov_reject` | fraction of updates discarded outright | `0.0` |
| `nis_accel` / `nis_mag` | is the filter's confidence honest? | `1.0` |

Frequently used flags — the full list is in
[src/ImudClient.h](src/ImudClient.h):

| Flag | Meaning |
|---|---|
| `IMUD_FLAG_FUSION_CONVERGED` | filter has settled — **don't trust attitude before this** |
| `IMUD_FLAG_DECLINATION_VALID` | declination known; gates `trueHeading()` |
| `IMUD_FLAG_MAG_VALID` | magnetometer healthy and calibrated |
| `IMUD_FLAG_HEAVE_VALID` / `IMUD_FLAG_WAVE_VALID` | heave / sea-state estimators settled |
| `IMUD_FLAG_SHUTDOWN` | daemon's final packet before a clean exit |

```cpp
if (p.flags & IMUD_FLAG_FUSION_CONVERGED)
    display.show(p.heading_deg);
```

For byte offsets and the full 276-byte layout, see
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## API reference

### `ImudClient`

The Arduino-facing wrapper. Owns no sockets — only pointers to
caller-owned transports — and never blocks except during an explicit or
throttled-automatic TCP reconnect.

| Method | Description |
|---|---|
| `bool beginTCP(Client &c, const char *host, uint16_t port = 10112)` | Also an `IPAddress` overload. Stores host/port for reconnects. Returns the initial connect result; a failed initial connect is fine — auto-reconnect takes over. |
| `bool beginUDP(UDP &u, uint16_t port = 10111, bool alreadyBound = false)` | If `alreadyBound` is false, calls `u.begin(port)`. For multicast, join first (see above) and pass `alreadyBound=true`. |
| `bool poll()` | Non-blocking. TCP: drains `available()` bytes (through a small stack chunk buffer) into the parser, and may trigger a throttled auto-reconnect while disconnected. UDP: drains every pending datagram. Returns `true` if ≥1 **new** valid packet arrived; `packet()` then holds the newest. |
| `const imud_packet_t &packet() const` | The newest valid packet decoded so far (zero-initialized before the first one). |
| `float trueHeading() const` | [`imud_true_heading()`](#imud_true_heading) applied to the current packet. `-1.0f` until declination is valid. |
| `bool connected() const` | TCP: `Client::connected()`. UDP: `true` once `beginUDP()` has been called. |
| `bool reconnect()` | TCP only: `stop()` + `connect()` to the stored host/port. Blocks like `Client::connect()` does. No-op (returns `false`) on UDP. |
| `void setAutoReconnect(bool on)` | Default **on**, minimum 2 s between attempts (`millis()`-based). `Client::connect()` blocks — sometimes for several seconds on ESP32 — so a control loop that can't tolerate that should `setAutoReconnect(false)` and call `reconnect()` when convenient. |
| `uint32_t millisSinceLastPacket() const` | Milliseconds since the last new valid packet, or `UINT32_MAX` before the first one — for watchdogs/UI staleness indicators. |
| `bool daemonShutdown() const` | `true` if the newest packet carried `IMUD_FLAG_SHUTDOWN` — the daemon's final packet before a clean exit. Display "daemon stopping" and suppress the reconnect alarm for this, rather than treating it as a dropped link. |
| `uint32_t packetsReceived() const` | Cumulative valid packets decoded. |
| `uint32_t crcErrors() const` | Cumulative validation failures (magic/version/size/CRC — despite the name, not CRC-only). |
| `uint32_t resyncs() const` | Cumulative magic re-scans after a validation failure on the TCP stream path. |
| `void end()` | Stops the transport and resets the parser (partial buffer + counters). |

### `ImudParser` (power users / testing)

Pure C++, zero Arduino includes, zero I/O — this is what `ImudClient` uses
internally, and what the [native unit tests](#testing) exercise directly.
Useful if you want to decode packets from a source `ImudClient` doesn't
cover (e.g. a custom transport, or a file of captured frames).

| Method | Description |
|---|---|
| `size_t feed(const uint8_t *data, size_t len)` | Streaming path (TCP): any chunking, one byte at a time is fine. Reassembles 276-byte frames and validates each as it completes; on failure, resynchronizes by dropping one byte and rescanning for the next magic sequence rather than discarding the whole buffer. Returns the count of **new** valid packets decoded this call. |
| `bool feedDatagram(const uint8_t *data, size_t len)` | Datagram path (UDP): no reassembly, no resync — a datagram is either a valid 276-byte packet or it's discarded. |
| `const imud_packet_t &packet() const` | Newest valid packet. |
| `uint32_t packetsReceived() / crcErrors() / resyncs() const` | Same semantics as on `ImudClient`. |
| `void reset()` | Drops the partial accumulation buffer and resets all counters. Does **not** clear `packet()`. |

### `imud_packet_t` and flags

The full 276-byte wire struct (accel/gyro/mag, quaternion, pitch/roll/yaw,
heading, rate of turn, covariance, heave, sea state, compass health, filter
gate health, …) and
the `IMUD_FLAG_*` bitmask are defined in `src/ImudClient.h`.

See [What's in a packet](#whats-in-a-packet) above for a field-by-field
table with units, or [docs/PROTOCOL.md](docs/PROTOCOL.md) for byte offsets
and the resync algorithm walkthrough.

### `imud_true_heading()`

```cpp
float imud_true_heading(const imud_packet_t *pkt);
```

Returns true (geographic) heading in `[0, 360)` when
`IMUD_FLAG_DECLINATION_VALID` is set, or `-1.0f` if declination isn't known
yet — or if the packet carries out-of-range values. Wire data is untrusted:
a crafted packet with Inf/NaN/1e38 here must not hang the caller (this was
found by fuzzing upstream), so the range check is written to fail closed on
NaN too. Ported verbatim from imud's reference implementation.

### `imud_rad_to_deg()`

```cpp
float imud_rad_to_deg(float rad);   // also: IMUD_RAD_TO_DEG
```

`pitch`, `roll` and `yaw` arrive in **radians**; `heading_deg` is the one
attitude field already in degrees. This converts:

```cpp
Serial.println(imud_rad_to_deg(p.roll));   // e.g. -7.2
```

## Protocol semantics

- **`heading_deg` is magnetic**, not true. Use [`trueHeading()`](#imud_true_heading)
  for the corrected value.
- **Flag-gated fields read `0.0` until their flag is set**: `heave_m` /
  `heave_rate` need `IMUD_FLAG_HEAVE_VALID`; `wave_height_m`, `wave_period_s`,
  `roll_period_s`, `roll_amplitude`, `pitch_period_s`, `pitch_amplitude` need
  `IMUD_FLAG_WAVE_VALID`; `declination_deg` needs
  `IMUD_FLAG_DECLINATION_VALID`.
- **`imu_seq` increments per daemon sample, not per packet you receive** —
  gaps are normal on both transports (a slow TCP client gets frames
  skipped; UDP can lose, duplicate, or reorder datagrams). Don't treat a
  gap as an error.
- **Don't trust the attitude before `IMUD_FLAG_FUSION_CONVERGED`** is set —
  the Kalman filter's covariance hasn't settled yet (e.g. right after the
  daemon starts, while `IMUD_FLAG_STARTUP` is also set).
- **The quaternion `[quat_w, quat_x, quat_y, quat_z]` is body→NED.**

## Server contract (what the client tolerates)

**TCP** (`[stream]` listener, default `:10112`, default 100 Hz):

- Broadcast-only: the daemon never reads from a client connection and never
  sends anything but whole 276-byte frames back-to-back. `ImudClient` never
  writes to the connection.
- Max 8 clients. A 9th connection is accepted, then immediately closed —
  **EOF right after connect means "server full."** `poll()`'s
  auto-reconnect backs off (minimum 2 s between attempts) and retries.
- A slow client's socket buffer filling up makes the daemon silently skip
  that frame for that client (a gap in `imu_seq`; the connection stays
  up). A client that errors or stalls completely gets closed by the
  daemon. `ImudClient` tolerates seq gaps and treats EOF/reset as
  "reconnect with backoff."
- Clean daemon shutdown sends one final packet with `IMUD_FLAG_SHUTDOWN`
  set, then closes the connection — see [`daemonShutdown()`](#imudclient).
- No protocol-level keepalive. If you need dead-link detection beyond
  `connected()`, poll [`millisSinceLastPacket()`](#imudclient) and pick
  your own staleness threshold (the daemon's default period is 10 ms;
  `examples/TcpBasic` uses 5 s as a starting point).

**UDP** (default `:10111`, up to 500 Hz):

- Every datagram is exactly one 276-byte packet; `ImudClient` drops
  oversized/undersized ones without reading them.
- Default daemon destination is multicast `239.255.0.1` — see
  [UDP multicast](#udp-multicast) above for joining. Unicast and broadcast
  reception need only a port bind.
- Loss, duplication, and reordering are all possible. Keeping "the latest
  valid packet" per `poll()` is the whole strategy — there's no
  reassembly or resync on this path because there's nothing to
  reassemble.

## Enabling the real daemon's outputs

Both transports are **off by default** on the daemon side. In
`/etc/imud/imud.conf`:

```ini
[stream]
tcp_enabled = true      # TCP listener, default port 10112

[highrate]
enabled = true           # UDP output, default port 10111
```

Restart the `imud` service after editing. See imud's own documentation for
the full set of `[stream]`/`[highrate]` options (bind address, port,
destination, rate).

## Testing

### Native unit tests (no hardware)

```sh
pio test -e native
```

Runs `test/test_parser/test_parser.cpp` against `ImudParser` directly, on
the host, using the golden vectors in `extras/golden/`: a bit-exact valid
packet (every field checked against `extras/golden/valid_packet.md`), a
corrupted-CRC packet, a version-mismatch packet, and a resync stream
(decoy magic + garbage + two valid frames, fed both whole and one byte at
a time) that must yield exactly two packets and never lose the following
frame.

### Live smoke test (a real client, no imud install needed)

```sh
python3 tools/fake_daemon.py                          # TCP listener on :10112
python3 tools/fake_daemon.py --udp 192.168.1.42:10111  # ... plus UDP
python3 tools/fake_daemon.py --rate 100                # packet rate (default 10 Hz)
```

`fake_daemon.py` faithfully mimics the real daemon's contract — the
8-client cap with immediate-close on the 9th, a SHUTDOWN-flag final packet
on Ctrl-C, and frame-skipping (seq gaps) for a slow client — and has been
verified against imud's reference Python client
(`resources/imud_client.py` upstream; useful as prose documentation of TCP
reassembly and as a second live consumer while testing). Point
`examples/TcpBasic` or `examples/UdpListen` at the machine running it and
you should see a smoothly sweeping heading. Try restarting the fake daemon
mid-run to watch the sketch reconnect, and Ctrl-C it to see the
daemon-shutdown message.

### CI

GitHub Actions (`.github/workflows/ci.yml`) runs the native test suite and
compiles both examples against `esp32dev`, `esp32-s3-devkitc-1`, and
`esp32-c3-devkitm-1` on every push/PR, plus a best-effort build for
`d1_mini` (ESP8266). RP2040 (Pico W) isn't covered in CI — see
[CONTRIBUTING.md's known CI gaps](CONTRIBUTING.md#known-ci-gaps) for why —
but the library's `Client`/`UDP`-only design still targets it; it just
isn't automatically verified.

A separate workflow (`.github/workflows/codeql.yml`) runs CodeQL static
analysis on push/PR and weekly: C/C++ over `ImudParser` (built via
`pio test -e native --without-testing`, the same native target the unit
tests use — this is what lets CodeQL trace real compiler invocations for a
header-only library with no top-level build system) and Python over
`tools/fake_daemon.py`. This is an "advanced setup" workflow, not GitHub's
default code-scanning autobuild — the default fails outright here since
there's nothing for it to autodetect.

## Wire-sync warning

This library pins **wire v17** and rejects any other version outright (see
[Protocol semantics](#protocol-semantics) and `docs/PROTOCOL.md`). When
imud revises its packet layout, it bumps the wire version — and this
library needs a synced struct plus a version bump before it can talk to
the new daemon. Wire bumps are coordinated from the imud repo (its
`AGENTS.md` wire-sync checklist is the source of truth); this repo is
listed there as a downstream consumer.

## Repository layout

```
imud-arduino/
  library.properties          # Arduino IDE/Library Manager manifest
  library.json                # PlatformIO manifest
  keywords.txt                # IDE syntax highlighting
  LICENSE                     # MIT
  src/ImudClient.h            # everything: ImudParser + ImudClient, header-only
  examples/
    HelloAttitude/            # START HERE: connect + print heading/pitch/roll
      HelloAttitude.ino
    TcpBasic/TcpBasic.ino     # WiFi + TCP: heading/roll/pitch/yaw, true heading,
                              #   staleness, reconnect
    UdpListen/UdpListen.ino   # multicast join + high-rate receive, rate/counters
  test/
    test_parser/test_parser.cpp  # PlatformIO native env + Unity
  extras/golden/               # golden wire-format test vectors
  tools/
    fake_daemon.py             # hardware-free test server (TCP+UDP, stdlib only)
    gen_vectors.py             # regenerates extras/golden/ for a new wire version
  docs/
    GETTING-STARTED.md         # beginner walkthrough, no hardware needed
    TROUBLESHOOTING.md         # symptom-first problem solving
    GLOSSARY.md                # NED, declination, heave, NIS, …
    PROTOCOL.md                # full field reference + resync algorithm
  platformio.ini
  .github/workflows/ci.yml
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Richard Simpson.
