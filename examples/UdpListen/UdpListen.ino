/*
 * UdpListen.ino — join imud's high-rate UDP multicast stream and print the
 * achieved packet rate once per second.
 *
 * By default imud's [highrate] UDP output targets multicast 239.255.0.1:
 * 10111 at up to 500 Hz (enable `[highrate] enabled = true` in
 * /etc/imud/imud.conf). UDP is lossy — packets can be dropped, duplicated,
 * or reordered — so this example just keeps "the latest valid packet" and
 * reports throughput, which is the whole strategy §3 of README.md
 * recommends for this transport.
 *
 * For a hardware-free test, point this at tools/fake_daemon.py running on
 * your dev machine:
 *
 *   python3 tools/fake_daemon.py --udp 239.255.0.1:10111 --rate 100
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ImudClient.h>

const char *WIFI_SSID = "your-ssid";
const char *WIFI_PASSWORD = "your-password";

const IPAddress IMUD_MULTICAST_GROUP(239, 255, 0, 1);
const uint16_t IMUD_PORT = 10111;

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
    // already-bound socket to ImudClient with alreadyBound=true. (ESP8266's
    // WiFiUDP::beginMulticast takes an extra interface-address argument;
    // adjust this line if porting to that core.)
    if (!udp.beginMulticast(IMUD_MULTICAST_GROUP, IMUD_PORT)) {
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

        const imud_packet_t &p = imud.packet();
        Serial.printf("rate=%5.1f Hz  seq=%-8lu hdg=%6.1f  total=%lu  "
                      "crc_err=%lu  resyncs=%lu\n",
                      rateHz, (unsigned long)p.imu_seq, p.heading_deg,
                      (unsigned long)total, (unsigned long)imud.crcErrors(),
                      (unsigned long)imud.resyncs());

        packetsAtLastReport = total;
        lastReport = now;
    }
}
