/*
 * HelloAttitude.ino — the simplest possible imud sketch: connect, then print
 * heading, pitch and roll to the Serial Monitor.
 *
 * START HERE if you are new to this library. There is nothing in this sketch
 * except "connect and print" — no reconnect handling, no error counters, no
 * staleness detection. Once this works, open the TcpBasic example, which
 * adds all of that for real-world use.
 *
 * WHAT YOU NEED
 *
 *   - A WiFi-capable board (ESP32 is the primary target; ESP8266 and RP2040
 *     Pico W also work).
 *   - A USB cable.
 *   - Either a real imud daemon on your network, OR — with no IMU hardware
 *     at all — the test server bundled with this library, run on your
 *     computer:
 *
 *         python3 tools/fake_daemon.py --rate 20
 *
 *   NO WIRING IS REQUIRED. This library talks to a daemon over the network;
 *   it does not read a sensor directly. Your board only needs power and WiFi.
 *
 * STEP BY STEP
 *
 *   1. Edit the four lines in the EDIT ME block below.
 *   2. Upload this sketch to your board.
 *   3. Open the Serial Monitor and set it to 115200 baud (see Serial.begin
 *      in setup() — the number there and the number in the Serial Monitor
 *      dropdown must match, or you will see nothing but garbage characters).
 *
 * If anything goes wrong, docs/TROUBLESHOOTING.md lists the usual causes by
 * symptom. The full walkthrough is in docs/GETTING-STARTED.md.
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

/* Each board core spells its WiFi header differently, so pick the right one.
 * You do not need to change anything here — the compiler chooses for you
 * based on the board you selected in the IDE. */
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_RP2040)
#include <WiFi.h>
#else
#error "This example needs a WiFi-capable core (ESP32, ESP8266, or RP2040 W)"
#endif
#include <ImudClient.h>

/* ─────────────────── EDIT THESE FOUR LINES, THEN UPLOAD ───────────────────*/

const char *WIFI_SSID     = "your-ssid";      // your WiFi network name
const char *WIFI_PASSWORD = "your-password";  // your WiFi password

/* The IP address of the computer or device running imud (or
 * fake_daemon.py). This is NOT your board's address. To find it, see
 * "Find your computer's IP" in docs/GETTING-STARTED.md — on macOS/Linux try
 * `ifconfig` or `ip addr`, on Windows `ipconfig`. Your board and that
 * machine must be on the same network. */
const char *IMUD_HOST     = "192.168.1.50";

/* imud's TCP port. 10112 is the default; only change this if you changed
 * `[stream] tcp_port` in the daemon's config. */
const uint16_t IMUD_PORT  = 10112;

/* ──────────────────────────────────────────────────────────────────────────*/

ImudClient imud;   // the library object: decodes packets, tracks the newest
WiFiClient net;    // the network socket it reads from; you own this, not it

unsigned long lastPrint = 0;

void setup() {
    /* 115200 is the speed the Serial Monitor must also be set to. */
    Serial.begin(115200);
    delay(200);  // give the USB serial link a moment to come up

    Serial.printf("\nConnecting to WiFi \"%s\"", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print('.');   // one dot per attempt, so you can see progress
    }
    Serial.printf("\nWiFi connected. This board's IP is %s\n",
                  WiFi.localIP().toString().c_str());

    /* beginTCP() remembers the host and port and opens the connection. It
     * returns false if that first attempt failed, but that is not fatal —
     * the library retries in the background, so we just report it. */
    Serial.printf("Connecting to imud at %s:%u ...\n", IMUD_HOST, IMUD_PORT);
    if (imud.beginTCP(net, IMUD_HOST, IMUD_PORT))
        Serial.println("Connected. Waiting for the first packet...");
    else
        Serial.println("Not connected yet — the library will keep retrying.");
}

void loop() {
    /* poll() does all the work: it reads whatever bytes have arrived, checks
     * each packet's magic number, wire version and CRC, and keeps the newest
     * valid one. It never blocks. It returns true only when at least one NEW
     * valid packet arrived since the last call — so everything inside this
     * `if` runs on fresh data. */
    if (imud.poll()) {
        const imud_packet_t &p = imud.packet();  // the newest valid packet

        /* Packets arrive far faster than anyone can read them (100 Hz on
         * TCP by default), so print about 5 times a second instead. */
        if (millis() - lastPrint >= 200) {
            lastPrint = millis();

            /* heading_deg is ALREADY in degrees and is MAGNETIC heading —
             * relative to magnetic north, not true north. pitch/roll/yaw
             * are in RADIANS, so convert them with imud_rad_to_deg(). */
            Serial.printf("heading %6.1f deg   pitch %6.1f deg   roll %6.1f deg\n",
                          p.heading_deg,
                          imud_rad_to_deg(p.pitch),
                          imud_rad_to_deg(p.roll));
        }
    }

    /* If nothing has arrived yet, say so once a second. A silent Serial
     * Monitor is ambiguous — it could mean "still connecting" or "wrong IP"
     * — so this makes the difference visible. */
    if (imud.packetsReceived() == 0 && millis() - lastPrint >= 1000) {
        lastPrint = millis();
        Serial.println("waiting for the first packet... "
                       "(is the daemon running? is IMUD_HOST right?)");
    }
}
