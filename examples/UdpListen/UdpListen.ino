/*
 * UdpListen.ino — join imud's high-rate UDP multicast stream, print attitude
 * plus the achieved packet rate once per second.
 *
 * NEW HERE? Start with HelloAttitude, then TcpBasic. UDP is the harder
 * transport to get working, because multicast has to survive your network.
 *
 * By default imud's [highrate] UDP output targets multicast 239.255.0.1:
 * 10111 at up to 500 Hz (enable `[highrate] enabled = true` in
 * /etc/imud/imud.conf). UDP is lossy — packets can be dropped, duplicated,
 * or reordered — so this example just keeps "the latest valid packet" and
 * reports throughput, which is the strategy the "Protocol semantics" section
 * of README.md recommends for this transport.
 *
 * HEADS UP: many consumer WiFi access points silently drop or rate-limit
 * multicast traffic. If the rate reads 0.0 Hz forever while TcpBasic works
 * fine, that is the most likely cause — see docs/TROUBLESHOOTING.md, which
 * covers testing with `--udp <your-board-ip>:10111` unicast instead.
 *
 * For a hardware-free test, point this at tools/fake_daemon.py running on
 * your dev machine:
 *
 *   python3 tools/fake_daemon.py --udp 239.255.0.1:10111 --rate 100
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_RP2040)
#include <WiFi.h>
#else
#error "This example needs a WiFi-capable core (ESP32, ESP8266, or RP2040 W)"
#endif
#include <WiFiUdp.h>
#include <ImudClient.h>

/* ─────────────────── EDIT THESE FOUR LINES, THEN UPLOAD ───────────────────*/

const char *WIFI_SSID = "your-ssid";
const char *WIFI_PASSWORD = "your-password";

/* Unlike TcpBasic there is no host address here: multicast means the board
 * subscribes to a GROUP that any sender on the network can publish to, so
 * you never name the daemon's IP. These two defaults match imud's
 * [highrate] section and usually need no change. */
const IPAddress IMUD_MULTICAST_GROUP(239, 255, 0, 1);
const uint16_t IMUD_PORT = 10111;

/* ──────────────────────────────────────────────────────────────────────────*/

ImudClient imud;
WiFiUDP udp;

uint32_t packetsAtLastReport = 0;
unsigned long lastReport = 0;

void connectWiFi() {
    Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print('.');
    }
    Serial.printf("\nWiFi connected, IP = %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);

    connectWiFi();

    // The abstract Arduino UDP class has no portable multicast join, so the
    // sketch joins first using the concrete WiFiUDP API, then hands the
    // already-bound socket to ImudClient with alreadyBound=true. ESP8266's
    // WiFiUDP::beginMulticast takes an extra interface-address argument
    // (its own IP) that ESP32/RP2040 don't need.
#if defined(ESP8266)
    bool joined = udp.beginMulticast(WiFi.localIP(), IMUD_MULTICAST_GROUP, IMUD_PORT);
#else
    bool joined = udp.beginMulticast(IMUD_MULTICAST_GROUP, IMUD_PORT);
#endif
    if (!joined) {
        Serial.println("Failed to join multicast group.");
    }
    imud.beginUDP(udp, IMUD_PORT, /*alreadyBound=*/true);

    Serial.printf("Listening for imud UDP on %s:%u\n",
                  IMUD_MULTICAST_GROUP.toString().c_str(), IMUD_PORT);

    lastReport = millis();
}

void loop() {
    imud.poll();  // drains every pending datagram; packet() holds the newest

    unsigned long now = millis();
    unsigned long elapsed = now - lastReport;
    if (elapsed >= 1000) {
        uint32_t total = imud.packetsReceived();
        uint32_t delta = total - packetsAtLastReport;
        float rateHz = delta * 1000.0f / (float)elapsed;

        if (total == 0) {
            // Nothing has EVER arrived. Don't print packet() here: it is
            // zero-initialized until the first valid packet lands, so it
            // would show a convincing-looking hdg=0.0 that means nothing.
            Serial.println("no packets yet -- is the daemon sending UDP? "
                           "does your access point pass multicast? "
                           "(see docs/TROUBLESHOOTING.md)");
        } else {
            const imud_packet_t &p = imud.packet();

            // heading_deg is already degrees (and magnetic); pitch/roll/yaw
            // are radians, so convert. See docs/GLOSSARY.md.
            Serial.printf("rate=%5.1f Hz  seq=%-8lu hdg=%6.1fdeg  "
                          "pitch=%6.1fdeg  roll=%6.1fdeg  yaw=%6.1fdeg  "
                          "total=%lu  crc_err=%lu  resyncs=%lu\n",
                          rateHz, (unsigned long)p.imu_seq, p.heading_deg,
                          imud_rad_to_deg(p.pitch), imud_rad_to_deg(p.roll),
                          imud_rad_to_deg(p.yaw),
                          (unsigned long)total, (unsigned long)imud.crcErrors(),
                          (unsigned long)imud.resyncs());
        }

        packetsAtLastReport = total;
        lastReport = now;
    }
}
