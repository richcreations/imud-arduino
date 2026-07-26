# Getting started

This guide takes you from a fresh copy of this library to live heading,
pitch and roll scrolling in your Serial Monitor.

**You do not need an IMU, and you do not need to wire anything up.**
`ImudClient` is a *network* client: it receives attitude data that the
[imud](https://github.com/richcreations/imud) daemon computes elsewhere and
publishes over TCP or UDP. Your board needs power and WiFi, nothing else.

If you don't have an imud daemon running yet, that's fine — this library
ships a test server that fakes one, so you can complete this whole guide
with just a board and a computer.

New terms (declination, NED, quaternion…) are defined in
[GLOSSARY.md](GLOSSARY.md). If something doesn't work, jump to
[TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## 1. What you need

- **A WiFi-capable board.** ESP32 is the primary target and the best-tested.
  ESP8266 and RP2040 (Pico W) also work. A plain Arduino Uno/Nano will *not*
  — it has no network hardware and not enough RAM.
- **A USB cable** that carries data. (Charge-only cables are a classic
  time-waster: if your board never appears as a serial port, try another
  cable before anything else.)
- **The Arduino IDE or PlatformIO** on your computer.
- **Python 3**, only for the test server in step 3. It is already installed
  on macOS and most Linux systems; Windows users can get it from
  python.org or the Microsoft Store.

## 2. Install the library

**Arduino IDE**

1. Download this repository as a ZIP (green "Code" button → Download ZIP).
2. In the IDE: **Sketch → Include Library → Add .ZIP Library…** and pick the
   file you just downloaded.
3. Restart the IDE.

**PlatformIO**

Add this to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/richcreations/imud-arduino.git
```

*Not sure which to use?* If you have never done this before, use the
Arduino IDE — it is simpler to set up. PlatformIO is worth switching to
later; it is what this project's own tests and CI use.

## 3. Start the test server

Open a terminal on your computer, `cd` into this repository, and run:

```
python3 tools/fake_daemon.py --rate 20
```

You should see:

```
TCP listener on 0.0.0.0:10112
```

That's it — it is now pretending to be an imud daemon, sending 20 packets a
second to anything that connects. Leave this running in its own terminal
window for the rest of the guide.

When your board connects, this window will also print `client (…) connected
(1)`, which is a handy confirmation that the network side is working.

> **Already have a real imud daemon?** Skip this step and use its IP address
> in step 5. Make sure `[stream] tcp_enabled = true` is set in
> `/etc/imud/imud.conf`.

## 4. Find your computer's IP address

This is the step people get stuck on most often. Your board needs the
address of the **computer running the test server** — not the board's own
address, and not `127.0.0.1`/`localhost` (from the board's point of view,
`localhost` means the board itself).

| Your OS | Command | What to look for |
|---|---|---|
| macOS | `ipconfig getifaddr en0` | the address it prints (try `en1` on WiFi-only Macs) |
| Linux | `ip addr` | the `inet` line under your WiFi interface, e.g. `wlan0` |
| Windows | `ipconfig` | "IPv4 Address" under your WiFi adapter |

You want something like `192.168.1.50` or `10.0.0.23`. Write it down.

**Both devices must be on the same network.** If your laptop is on a 5 GHz
band and your ESP32 only joins 2.4 GHz, that is usually still the same
network — but a "guest" WiFi network is typically isolated and will not
work.

## 5. Open, edit and upload the example

Open **File → Examples → ImudClient → HelloAttitude**.
(In PlatformIO, copy `examples/HelloAttitude/HelloAttitude.ino` into your
project's `src/`.)

Find the `EDIT THESE FOUR LINES` block near the top and fill in your
details:

```cpp
const char *WIFI_SSID     = "your-ssid";       // <- your WiFi name
const char *WIFI_PASSWORD = "your-password";   // <- your WiFi password
const char *IMUD_HOST     = "192.168.1.50";    // <- the IP from step 4
const uint16_t IMUD_PORT  = 10112;             // <- leave this alone
```

Then:

1. **Tools → Board** → select your board (e.g. "ESP32 Dev Module").
2. **Tools → Port** → select the port your board appears on. If no port
   appears, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
3. Click **Upload** and wait for "Done uploading".

## 6. Open the Serial Monitor at 115200

**Tools → Serial Monitor**, then set the speed dropdown (bottom right) to
**115200 baud**.

This number must match the `Serial.begin(115200)` in the sketch. If it
doesn't, you get streams of garbage characters like `��@�` rather than text
— that symptom means *only* that the baud rate is wrong, nothing else.

You should now see:

```
Connecting to WiFi "your-ssid".....
WiFi connected. This board's IP is 192.168.1.87
Connecting to imud at 192.168.1.50:10112 ...
Connected. Waiting for the first packet...
heading    0.4 deg   pitch    0.0 deg   roll    0.0 deg
heading    0.5 deg   pitch    0.0 deg   roll    0.0 deg
heading    0.6 deg   pitch    0.0 deg   roll    0.0 deg
```

**That's success.** The test server sweeps heading slowly (0.5°/second) and
reports level pitch and roll, so a slowly climbing heading with pitch and
roll pinned at 0.0 is exactly right. With a real daemon connected to real
hardware, all three will move as you tilt the sensor.

If you see `waiting for the first packet...` repeating, the board is on WiFi
but cannot reach the server — check the IP from step 4 first, then
[TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## Where to go next

- **[TcpBasic](../examples/TcpBasic/TcpBasic.ino)** — the same thing, built
  for real use: auto-reconnect, staleness detection, true heading, and error
  counters. Try stopping and restarting `fake_daemon.py` while it runs to
  watch the reconnect logic work.
- **[UdpListen](../examples/UdpListen/UdpListen.ino)** — the high-rate
  multicast path, for when you want hundreds of updates per second.
- **[What's in a packet](../README.md#whats-in-a-packet)** — everything else
  a packet carries beyond heading/pitch/roll.
- **[GLOSSARY.md](GLOSSARY.md)** — what declination, NED, heave and the rest
  actually mean.
- **[Enabling the real daemon's outputs](../README.md#enabling-the-real-daemons-outputs)**
  — when you're ready to point this at real hardware.
- **[PROTOCOL.md](PROTOCOL.md)** — the byte-level wire format. You only need
  this if you're extending the library or writing your own parser.
