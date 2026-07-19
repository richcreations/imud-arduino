/*
 * TcpBasic.ino — connect to an imud daemon's TCP stream listener and print
 * attitude data.
 *
 * The TCP path is lossless and framed: every valid packet the daemon sends
 * is delivered in order, with gaps in imu_seq only when the daemon itself
 * skips a slow client's frame (see README.md, "Server contract"). This
 * example also demonstrates staleness detection and auto-reconnect.
 *
 * Wiring: none — this is a network-only example. Point it at a real imud
 * daemon (enable `[stream] tcp_enabled = true` in /etc/imud/imud.conf) or,
 * for a hardware-free test, at tools/fake_daemon.py running on your dev
 * machine:
 *
 *   python3 tools/fake_daemon.py --rate 20
 *
 * Then set IMUD_HOST below to that machine's IP address. Try stopping and
 * restarting fake_daemon.py while the sketch runs to see the reconnect
 * logic recover, and Ctrl-C it to see the SHUTDOWN-flag packet reported.
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#include <WiFi.h>
#include <ImudClient.h>

const char *WIFI_SSID = "your-ssid";
const char *WIFI_PASSWORD = "your-password";

const char *IMUD_HOST = "192.168.1.50";  // fake_daemon.py / imud host
const uint16_t IMUD_PORT = 10112;        // [stream] tcp_port, default 10112

ImudClient imud;
WiFiClient net;

unsigned long lastPrint = 0;
unsigned long lastLinkWarning = 0;

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

    // setAutoReconnect(true) is the default: poll() will call reconnect()
    // for us (at most once every 2 s) whenever the link is down. Client::
    // connect() blocks — sometimes for several seconds on ESP32 — so a
    // control loop that can't tolerate that should setAutoReconnect(false)
    // and call imud.reconnect() explicitly when it's convenient.
    Serial.printf("Connecting to imud at %s:%u (TCP)...\n", IMUD_HOST, IMUD_PORT);
    if (imud.beginTCP(net, IMUD_HOST, IMUD_PORT)) {
        Serial.println("Connected.");
    } else {
        Serial.println("Initial connect failed; auto-reconnect will keep trying.");
    }
}

void loop() {
    if (imud.poll()) {
        const imud_packet_t &p = imud.packet();

        if (imud.daemonShutdown()) {
            // Final packet before the daemon's clean exit (Ctrl-C on the
            // fake or real daemon) — not a dropped link, so don't sound the
            // reconnect alarm for this one.
            Serial.println(">>> imud daemon reported a clean shutdown.");
        }

        if (millis() - lastPrint >= 200) {
            lastPrint = millis();
            float trueHdg = imud.trueHeading();  // -1.0f until declination valid

            Serial.printf("seq=%-8lu hdg=%6.1f  roll=%6.1f  pitch=%6.1f  ",
                          (unsigned long)p.imu_seq, p.heading_deg,
                          p.roll * 57.29578f, p.pitch * 57.29578f);

            if (trueHdg >= 0.0f)
                Serial.printf("true_hdg=%6.1f  ", trueHdg);
            else
                Serial.print("true_hdg=   n/a  ");

            Serial.printf("converged=%s  pkts=%lu crc_err=%lu resyncs=%lu\n",
                          (p.flags & IMUD_FLAG_FUSION_CONVERGED) ? "yes" : "no ",
                          (unsigned long)imud.packetsReceived(),
                          (unsigned long)imud.crcErrors(),
                          (unsigned long)imud.resyncs());
        }
    }

    // Link-health check, independent of whether poll() saw a new packet
    // this iteration. millisSinceLastPacket() is UINT32_MAX before the
    // first packet ever arrives.
    uint32_t staleMs = imud.millisSinceLastPacket();
    bool stale = (staleMs != UINT32_MAX) && (staleMs > 5000);
    if ((stale || !imud.connected()) && millis() - lastLinkWarning >= 1000) {
        lastLinkWarning = millis();
        if (!imud.connected())
            Serial.println("Link down -- auto-reconnect is retrying "
                            "(server-full or network drop both look like this).");
        else
            Serial.printf("No valid packet for %lu ms -- link may be stale.\n",
                          (unsigned long)staleMs);
    }
}
