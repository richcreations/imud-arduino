/*
 * TcpBasic.ino — connect to an imud daemon's TCP stream listener and print
 * attitude data, with the error handling a real deployment needs.
 *
 * NEW HERE? Start with the HelloAttitude example instead — it is the same
 * connect-and-print idea with nothing else in the way. Come back to this one
 * once that works: it adds auto-reconnect, staleness detection, clean-shutdown
 * handling, and the parser's error counters.
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

const char *WIFI_SSID = "your-ssid";
const char *WIFI_PASSWORD = "your-password";

/* The address of the machine running imud or fake_daemon.py — not your
 * board's own address. See docs/GETTING-STARTED.md if you need to find it. */
const char *IMUD_HOST = "192.168.1.50";  // fake_daemon.py / imud host
const uint16_t IMUD_PORT = 10112;        // [stream] tcp_port, default 10112

/* ──────────────────────────────────────────────────────────────────────────*/

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

            // heading_deg is already degrees and is MAGNETIC (relative to
            // magnetic north). roll/pitch/yaw are radians, hence the
            // imud_rad_to_deg() calls. See docs/GLOSSARY.md.
            Serial.printf("seq=%-8lu hdg=%6.1fdeg  roll=%6.1fdeg  "
                          "pitch=%6.1fdeg  yaw=%6.1fdeg  ",
                          (unsigned long)p.imu_seq, p.heading_deg,
                          imud_rad_to_deg(p.roll), imud_rad_to_deg(p.pitch),
                          imud_rad_to_deg(p.yaw));

            // True (geographic) heading = magnetic heading + declination.
            // Reads n/a until the daemon knows the local declination.
            if (trueHdg >= 0.0f)
                Serial.printf("true_hdg=%6.1fdeg  ", trueHdg);
            else
                Serial.print("true_hdg=   n/a  ");

            // converged: the filter has settled — don't trust attitude before
            //            this reads yes.
            // crc_err:   packets that failed validation. A few during a
            //            reconnect is normal; a steadily climbing count means
            //            a genuinely bad link (or a wire-version mismatch).
            // resyncs:   times the parser had to hunt for the next frame
            //            boundary after a bad packet. Expect 0 on a healthy
            //            TCP link; it rises alongside crc_err, not on its own.
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
