# Troubleshooting

Organized by **what you're seeing**, since that's what you know when you get
stuck. New to the library? Work through
[GETTING-STARTED.md](GETTING-STARTED.md) first — most problems below are a
step from that guide that got skipped.

- [Nothing at all in the Serial Monitor](#nothing-at-all-in-the-serial-monitor)
- [Garbage characters instead of text](#garbage-characters-instead-of-text)
- [No serial port to select](#no-serial-port-to-select)
- [Stuck on "Connecting to WiFi"](#stuck-on-connecting-to-wifi)
- [WiFi connects, but no packets ever arrive](#wifi-connects-but-no-packets-ever-arrive)
- [Everything reads 0.0 forever](#everything-reads-00-forever)
- [true_hdg reads n/a forever](#true_hdg-reads-na-forever)
- [converged=no](#convergedno)
- [crc_err is climbing](#crc_err-is-climbing)
- [resyncs is climbing](#resyncs-is-climbing)
- [UDP: rate stays 0.0 Hz](#udp-rate-stays-00-hz)
- [The connection drops after a while](#the-connection-drops-after-a-while)
- [Compile error: "needs a WiFi-capable core"](#compile-error-needs-a-wifi-capable-core)
- [Compile error: no member named nis_accel](#compile-error-no-member-named-nis_accel)

---

## Nothing at all in the Serial Monitor

**Most likely:** the Serial Monitor was opened before the board finished
booting, or the board hasn't been reset since upload.

1. Check the baud dropdown reads **115200**.
2. Press the **RST/EN** button on the board — the sketch restarts and prints
   its opening lines again.
3. Confirm the upload actually succeeded ("Done uploading" / "SUCCESS").
4. On some ESP32 boards the USB serial link takes a moment; the examples
   already `delay(200)` after `Serial.begin()` for this reason.

## Garbage characters instead of text

Output like `����@�x��` means exactly one thing: **the baud rate doesn't
match.** Set the Serial Monitor to **115200**, the same number as
`Serial.begin(115200)` in the sketch. Nothing else causes this specific
symptom, so don't go looking further.

## No serial port to select

1. **Try a different USB cable.** Many cheap cables carry power only, and
   this is the single most common cause.
2. Some boards (especially ESP32 clones with CP210x or CH340 chips) need a
   **USB-serial driver** installed on your computer. Search for your board's
   chip name plus "driver".
3. Try a different USB port, ideally directly on the computer rather than
   through a hub.

## Stuck on "Connecting to WiFi"

The sketch prints one dot per attempt, so an endless row of dots
(`Connecting to WiFi "home"........`) means the join is failing.

- **Check the SSID and password for typos**, including capitalisation.
- **ESP32 and ESP8266 only support 2.4 GHz WiFi.** If your network is
  5 GHz-only, the board cannot see it. Many routers publish both bands under
  one name, which usually works.
- **Enterprise/captive-portal networks** (workplace, university, hotel) need
  more than an SSID and password and will not work with these examples.
- Move the board closer to the access point.

## WiFi connects, but no packets ever arrive

You see the board's own IP printed, then `waiting for the first packet...`
repeating. The network stack is fine; the board can't reach the daemon.

Work down this list in order:

1. **Is `IMUD_HOST` the right address?** It must be the IP of the machine
   running imud or `fake_daemon.py` — **not** the board's own IP (which the
   sketch prints, and which is a different number), and never `127.0.0.1`
   or `localhost`.
2. **Is the server actually running?** The `fake_daemon.py` terminal should
   still be showing `TCP listener on 0.0.0.0:10112`. It also prints
   `client (…) connected` when your board reaches it — if that line never
   appears, nothing is getting through.
3. **Same network?** A "guest" WiFi network is usually isolated from the
   main one on purpose. Client isolation / "AP isolation" on the router does
   the same thing.
4. **Firewall.** macOS and Windows firewalls commonly block incoming
   connections to Python. Allow it, or test by temporarily disabling the
   firewall. On Linux, check `ufw`/`firewalld` for port 10112.
5. **Right port?** 10112 for TCP, 10111 for UDP. If you changed
   `[stream] tcp_port` in the daemon config, match it here.
6. **Real daemon only:** confirm `[stream] tcp_enabled = true` in
   `/etc/imud/imud.conf`, and that the daemon is running.

A quick way to isolate the board from the equation: from another computer on
the same network, run `telnet <host> 10112` or `nc <host> 10112`. If that
also hangs, the problem is the server or the network, not your sketch.

## Everything reads 0.0 forever

`heading`, `pitch` and `roll` all sit at exactly `0.0` and never move.

**If no packet has ever arrived**, this is a display artifact: `packet()` is
zero-filled until the first valid packet lands. `HelloAttitude` and
`UdpListen` guard against this, but a sketch of your own might not. Check
`imud.packetsReceived() > 0` before trusting any field, then work through
[no packets ever arrive](#wifi-connects-but-no-packets-ever-arrive).

**If packets *are* arriving** (`pkts=` is climbing), then the daemon really
is reporting zeros — look at the daemon end, not the client.

## true_hdg reads n/a forever

Working as designed. True heading needs the local **magnetic declination**,
and the library refuses to guess: `imud_true_heading()` returns `-1.0f`
whenever `IMUD_FLAG_DECLINATION_VALID` is clear, rather than silently
handing you a wrong bearing. See [GLOSSARY.md](GLOSSARY.md#declination).

Set the declination in the daemon's configuration. Note that
`fake_daemon.py` *does* set it (to 11.25°), so with the test server you
should see a real number — `true_hdg` exactly 11.25 higher than `hdg`.

## converged=no

The attitude filter hasn't settled yet. Give it 10–30 seconds after the
daemon starts. **Don't trust attitude while this reads `no`.**

If it never converges on real hardware, the daemon end needs attention —
typically magnetometer calibration or a sensor mounted near something
magnetic.

## crc_err is climbing

Each count is a packet that failed validation and was discarded.

- **A handful during a reconnect is normal** — a partial frame left in the
  buffer when the link dropped will fail once, then resync.
- **Steadily climbing** means either a genuinely lossy link, or — much more
  likely — a **wire version mismatch**. See
  [no packets at all](#compile-error-no-member-named-nis_accel) below: this
  library version pins wire v17 and needs imud ≥ 1.7.
- Note that a version mismatch increments `crc_err` even though the CRC
  itself was fine; the counter covers all validation failures, not just CRC.

## resyncs is climbing

The parser lost frame alignment and had to hunt for the next packet
boundary. This essentially always rises *alongside* `crc_err` and for the
same reasons — treat it as a symptom, not a separate fault. On a healthy TCP
link both stay at 0.

`resyncs` rising while `crc_err` stays flat would be unusual and worth
[reporting as a bug](../CONTRIBUTING.md#reporting-bugs).

## UDP: rate stays 0.0 Hz

Almost always **multicast being dropped by your network**. Many consumer
access points silently discard or heavily rate-limit multicast traffic, and
some drop it entirely when a client is in power-save mode.

To confirm it's the network and not your sketch, bypass multicast entirely
and send UDP straight to the board's own IP (the one it prints at startup):

```
python3 tools/fake_daemon.py --udp 192.168.1.87:10111 --rate 100
```

If unicast works and multicast doesn't, it's the access point. Options: use
the TCP path instead (`TcpBasic`), use unicast UDP, or enable IGMP
snooping/multicast forwarding on the router if it offers it.

Also worth checking: `[highrate] enabled = true` in the daemon config, and
that the group and port match on both ends.

## The connection drops after a while

- **`Link down -- auto-reconnect is retrying`** in TcpBasic is often the
  daemon reaching its **8-client limit**. The 9th client is accepted and
  then immediately closed, which looks identical to a network drop from the
  client side. Close any other connected clients.
- imud sends a final packet with the SHUTDOWN flag before exiting cleanly.
  TcpBasic reports this as `>>> imud daemon reported a clean shutdown.` —
  that's the daemon stopping on purpose, not a fault.
- WiFi power-save on ESP32 can cause periodic stalls. `WiFi.setSleep(false)`
  in `setup()` trades power for a steadier link.

## Compile error: "needs a WiFi-capable core"

```
#error "This example needs a WiFi-capable core (ESP32, ESP8266, or RP2040 W)"
```

The examples need a board with networking. An Arduino Uno, Nano, or Mega
won't work. Select an ESP32, ESP8266, or Pico W board under **Tools →
Board**.

If you're using an Ethernet shield rather than WiFi, the *library* supports
that fine — it works with any `Client`/`UDP` implementation — but the
examples are written for WiFi. Swap `WiFiClient` for `EthernetClient` and
drop the WiFi-join code.

## Compile error: no member named nis_accel

You're compiling an example from this version against an older copy of
`ImudClient.h`. The Arduino IDE keeps libraries in your sketchbook
`libraries/` folder — an old ImudClient there will shadow the one you think
you installed. Delete the old copy and re-install.

**The related runtime trap:** this library version speaks **wire v17** and
requires **imud ≥ 1.7**. Against an imud 1.4–1.6 daemon it receives
*nothing at all* — no error, no packets, just silence — because every packet
fails the version check. If you're on an older daemon, use the ImudClient
1.0.x line instead. See [the wire-sync
warning](../README.md#wire-sync-warning).

---

## Still stuck?

Open an issue — see [Reporting bugs](../CONTRIBUTING.md#reporting-bugs) for
what to include. A copy of your Serial Monitor output, your board type, and
your imud version make this much faster to diagnose.
