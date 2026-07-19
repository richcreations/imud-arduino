/*
 * ImudClient.h — Arduino client library for the imud IMU daemon
 *
 * Receives, validates, and decodes imud's 260-byte binary attitude packets
 * (wire v14) over TCP (lossless, framed) or UDP (unicast/broadcast/
 * multicast, higher rate). Works with any Arduino Client/UDP transport —
 * WiFiClient/WiFiUDP, EthernetClient/EthernetUDP, etc. ESP32 is the primary
 * target; the library also compiles for ESP8266, RP2040 (Pico W), and
 * Ethernet-shield boards.
 *
 * QUICK START
 *
 *   #include <ImudClient.h>
 *   #include <WiFi.h>
 *
 *   ImudClient imud;
 *   WiFiClient net;
 *
 *   void setup() {
 *       WiFi.begin(ssid, password);
 *       while (WiFi.status() != WL_CONNECTED) delay(100);
 *       imud.beginTCP(net, "192.168.1.50", 10112);
 *   }
 *
 *   void loop() {
 *       if (imud.poll()) {
 *           const imud_packet_t &p = imud.packet();
 *           Serial.println(p.heading_deg);
 *       }
 *   }
 *
 * See examples/TcpBasic and examples/UdpListen for complete sketches, and
 * README.md for the full API reference and protocol semantics.
 *
 * WIRE-SYNC WARNING
 *
 * This library pins wire v14 and rejects any other version. When imud
 * revises the packet layout it bumps the wire version, and this library
 * needs a synced struct + version update before it can talk to the new
 * daemon. See README.md for details.
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#ifndef IMUD_CLIENT_H
#define IMUD_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Packets are decoded by overlaying a packed struct directly onto the
 * received bytes, with no byte-order conversion; the wire format is
 * little-endian, so a little-endian target is required. True of every
 * platform this library targets (ESP32, ESP8266, RP2040, AVR). */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
# error "imud's binary wire format requires a little-endian target"
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Protocol constants
 * ───────────────────────────────────────────────────────────────────────*/

#define IMUD_MAGIC        0x494D5544u   /* "IMUD" */
/* Wire-layout revision, NOT the release version. 14 = layout introduced in
 * imud 1.4, unchanged through 1.6. This library rejects any other value —
 * see the wire-sync warning above and in README.md. */
#define IMUD_VERSION      14
#define IMUD_PACKET_SIZE  260           /* bytes, fixed */

/* ─────────────────────────────────────────────────────────────────────────
 * Packet flags (bitmask in imud_packet_t.flags)
 * ───────────────────────────────────────────────────────────────────────*/

#define IMUD_FLAG_MAG_VALID         (1u << 0)  /* mag healthy and calibrated */
#define IMUD_FLAG_MAG_SET_RESET     (1u << 1)  /* SET pulse within last read */
#define IMUD_FLAG_FUSION_CONVERGED  (1u << 2)  /* filter covariance settled */
#define IMUD_FLAG_ACCEL_CAL         (1u << 3)  /* accel calibration applied */
#define IMUD_FLAG_GYRO_CAL          (1u << 4)  /* gyro bias applied */
#define IMUD_FLAG_MAG_CAL           (1u << 5)  /* mag hard/soft-iron applied */
#define IMUD_FLAG_MOTION            (1u << 6)  /* reserved — never set in v14 */
#define IMUD_FLAG_FIFO_OVERFLOW     (1u << 7)  /* sample gap (FIFO overflow) */
#define IMUD_FLAG_STARTUP           (1u << 8)  /* gyro bias est. in progress */
#define IMUD_FLAG_SHUTDOWN          (1u << 9)  /* final packet before exit */
#define IMUD_FLAG_DECLINATION_VALID (1u << 10) /* declination known */
#define IMUD_FLAG_HEAVE_VALID       (1u << 11) /* heave estimator settled */
#define IMUD_FLAG_WAVE_VALID        (1u << 12) /* sea-state stats settled */
#define IMUD_FLAG_ENGINE_ON         (1u << 13) /* engine-vibration detected */

/* ─────────────────────────────────────────────────────────────────────────
 * Wire packet — 260 bytes, little-endian, fixed size.
 *
 * WIRE-SYNC WARNING: this struct is copied verbatim from imud's reference
 * implementation for wire v14. imud pins this layout and only changes it on
 * a wire-version bump (see IMUD_VERSION above). Do not hand-edit field
 * order, types, or count without a corresponding version bump and a fresh
 * copy from the upstream reference — a mismatched struct silently decodes
 * garbage into named fields.
 * ───────────────────────────────────────────────────────────────────────*/

#if defined(_MSC_VER)
#  pragma pack(push, 1)
#  define IMUD_PACKED_ATTR
#elif defined(__GNUC__) || defined(__clang__)
#  define IMUD_PACKED_ATTR __attribute__((packed))
#else
#  define IMUD_PACKED_ATTR
#endif

typedef struct IMUD_PACKED_ATTR {
    /* Header */
    uint32_t magic;           /* IMUD_MAGIC */
    uint16_t version;         /* IMUD_VERSION */
    uint16_t flags;           /* IMUD_FLAG_* bitmask */
    uint64_t ts_wall_ns;      /* CLOCK_REALTIME, nanoseconds */
    uint64_t ts_tai_ns;       /* CLOCK_TAI, nanoseconds */
    uint32_t ts_chip_ticks;   /* IMU hardware counter */
    uint32_t anchor_gen;      /* increments on wall-clock re-anchor */
    /* Accelerometer — m/s² */
    float accel_x;            /* calibrated */
    float accel_y;
    float accel_z;
    float accel_raw_x;        /* pre-calibration */
    float accel_raw_y;
    float accel_raw_z;
    /* Gyroscope — rad/s */
    float gyro_x;             /* bias-corrected */
    float gyro_y;
    float gyro_z;
    float gyro_raw_x;         /* before bias correction */
    float gyro_raw_y;
    float gyro_raw_z;
    /* Magnetometer — µT */
    float mag_x;              /* calibrated */
    float mag_y;
    float mag_z;
    float mag_raw_x;          /* pre-calibration */
    float mag_raw_y;
    float mag_raw_z;
    /* Fused attitude */
    float quat_w;             /* unit quaternion [w, x, y, z], body→NED */
    float quat_x;
    float quat_y;
    float quat_z;
    float pitch;              /* rad, NED (+bow up) */
    float roll;               /* rad, NED (+starboard up) */
    float yaw;                /* rad, NED magnetic */
    float heading_deg;        /* 0–360° magnetic */
    float rate_of_turn;       /* deg/min, + = turning right */
    float temp_c;             /* IMU die temperature, °C */
    float cov[9];             /* 3×3 attitude error covariance, row-major */
    uint32_t imu_seq;         /* monotonic sample counter */
    float    declination_deg; /* °E+; 0.0 when DECLINATION_VALID not set */
    float    heave_m;         /* vertical displacement, m, + up */
    float    gyro_bias_x;     /* estimated gyro bias, rad/s */
    float    gyro_bias_y;
    float    gyro_bias_z;
    float    gyro_bias_var_x; /* gyro-bias variance, (rad/s)² */
    float    gyro_bias_var_y;
    float    gyro_bias_var_z;
    float    heave_rate;      /* vertical velocity, m/s, + up */
    float    accel_quiescence;/* EMA of (|a|/g − 1)² */
    float    wave_height_m;   /* significant wave height Hs, m */
    float    wave_period_s;   /* mean zero-crossing wave period Tz, s */
    float    roll_period_s;   /* vessel roll period, s; 0 = not rolling */
    float    roll_amplitude;  /* significant single amplitude 2σ(roll), rad */
    float    pitch_period_s;  /* vessel pitch period, s */
    float    pitch_amplitude; /* significant single amplitude 2σ(pitch), rad */
    float    mag_anomaly;     /* EMA of ||B|−|B_ref||/|B_ref| (unitless) */
    float    mag_residual;    /* EMA of |heading innovation|, rad */
    uint32_t crc32;           /* IEEE 802.3 CRC32 of bytes 0–255 */
} imud_packet_t;

#if defined(_MSC_VER)
#  pragma pack(pop)
#endif

/* The struct is naturally aligned (no implicit padding), so `packed` above
 * is a guard against a future field breaking that, not a behavior change —
 * field access is aligned. */
static_assert(sizeof(imud_packet_t) == IMUD_PACKET_SIZE,
              "imud_packet_t mis-packed");

/* ─────────────────────────────────────────────────────────────────────────
 * CRC32 — IEEE 802.3 / zlib polynomial, bitwise (no table: 260 bytes at
 * 240 MHz is microseconds; deliberately not pulling in a CRC library).
 * ───────────────────────────────────────────────────────────────────────*/

inline uint32_t imud_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ─────────────────────────────────────────────────────────────────────────
 * imud_true_heading — true (geographic) heading from a packet.
 *
 * Returns the true heading in [0, 360) when IMUD_FLAG_DECLINATION_VALID is
 * set, or -1.0f if the declination is not yet known — or if the packet
 * carries out-of-range values. Wire data is untrusted: a crafted packet
 * with Inf/NaN/1e38 here must not hang the caller (upstream found this by
 * fuzzing). The comparison below is written so NaN fails it too. Ported
 * verbatim from imud's reference implementation — do not "simplify" the
 * range check or the wraparound loop.
 * ───────────────────────────────────────────────────────────────────────*/

inline float imud_true_heading(const imud_packet_t *pkt) {
    if (!(pkt->flags & IMUD_FLAG_DECLINATION_VALID))
        return -1.0f;
    float h = pkt->heading_deg + pkt->declination_deg;
    if (!(h > -720.0f && h < 720.0f))
        return -1.0f;
    h += 720.0f;                       /* → (0, 1440) */
    while (h >= 360.0f) h -= 360.0f;   /* at most 3 iterations */
    return h;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ImudParser — pure C++, zero Arduino includes, zero I/O.
 *
 * Feed it raw bytes from any source (TCP stream, UDP datagram, a unit
 * test's byte array) and it decodes and validates imud packets, keeping
 * the newest valid one. This is what makes the library testable on a host
 * PC with no mocks: it depends on nothing but the C++ standard library.
 * ───────────────────────────────────────────────────────────────────────*/

class ImudParser {
public:
    ImudParser()
        : fill_(0), pkt_(), packetsReceived_(0), crcErrors_(0), resyncs_(0) {
        memset(&pkt_, 0, sizeof(pkt_));
    }

    /* Feed raw stream bytes (any chunking — one byte at a time is fine).
     * Reassembles 260-byte frames, validating each as it completes; on
     * failure, resynchronizes by dropping one byte and rescanning for the
     * next magic sequence rather than discarding the whole buffer, so a
     * valid frame that starts partway through never gets lost.
     * Returns the number of NEW valid packets decoded during this call;
     * packet() then holds the newest one. */
    size_t feed(const uint8_t *data, size_t len) {
        size_t newCount = 0;
        for (size_t i = 0; i < len; i++) {
            buf_[fill_++] = data[i];
            if (fill_ == IMUD_PACKET_SIZE) {
                if (validate(buf_, IMUD_PACKET_SIZE)) {
                    memcpy(&pkt_, buf_, IMUD_PACKET_SIZE);
                    fill_ = 0;
                    packetsReceived_++;
                    newCount++;
                } else {
                    crcErrors_++;
                    resyncScan();
                }
            }
        }
        return newCount;
    }

    /* Feed exactly one whole UDP datagram: no reassembly, no resync — a
     * datagram is either a valid 260-byte packet or it is discarded.
     * Returns true if it was accepted; packet() then holds it. */
    bool feedDatagram(const uint8_t *data, size_t len) {
        if (!validate(data, len)) {
            crcErrors_++;
            return false;
        }
        memcpy(&pkt_, data, IMUD_PACKET_SIZE);
        packetsReceived_++;
        return true;
    }

    /* Newest valid packet decoded so far. Zero-initialized before the
     * first one arrives. */
    const imud_packet_t &packet() const { return pkt_; }

    uint32_t packetsReceived() const { return packetsReceived_; }
    uint32_t crcErrors() const { return crcErrors_; }      /* validation failures */
    uint32_t resyncs() const { return resyncs_; }          /* magic re-scans after failure */

    /* Drop the partial accumulation buffer and reset all counters. Does
     * NOT clear the last decoded packet(). */
    void reset() {
        fill_ = 0;
        packetsReceived_ = 0;
        crcErrors_ = 0;
        resyncs_ = 0;
    }

private:
    /* Validation order (normative): size, magic, version, CRC32. */
    static bool validate(const uint8_t *data, size_t len) {
        if (len != IMUD_PACKET_SIZE)
            return false;

        uint32_t magic;
        memcpy(&magic, data, sizeof(magic));
        if (magic != IMUD_MAGIC)
            return false;

        uint16_t version;
        memcpy(&version, data + 4, sizeof(version));
        if (version != IMUD_VERSION)
            return false;

        uint32_t stored;
        memcpy(&stored, data + offsetof(imud_packet_t, crc32), sizeof(stored));
        return imud_crc32(data, offsetof(imud_packet_t, crc32)) == stored;
    }

    /* Called after a full 260-byte buffer fails validation. Drops exactly
     * one byte, then scans the remaining buffer for the next magic
     * sequence, discarding everything before it. If no full match exists,
     * retains a trailing partial match (1-3 bytes) so a magic sequence
     * split across feed() calls can still complete; only discards
     * everything if nothing recoverable remains. Never resets the whole
     * buffer as the default action — a valid frame may start inside it. */
    void resyncScan() {
        resyncs_++;
        if (fill_ == 0)
            return;

        memmove(buf_, buf_ + 1, fill_ - 1);
        fill_ -= 1;

        static const uint8_t kMagic[4] = { 0x44, 0x55, 0x4D, 0x49 };

        for (size_t i = 0; i + 4 <= fill_; i++) {
            if (memcmp(buf_ + i, kMagic, 4) == 0) {
                if (i > 0) {
                    memmove(buf_, buf_ + i, fill_ - i);
                    fill_ -= i;
                }
                return;
            }
        }

        size_t start = (fill_ > 3) ? fill_ - 3 : 0;
        for (; start < fill_; start++) {
            size_t matchLen = fill_ - start;
            if (memcmp(buf_ + start, kMagic, matchLen) == 0) {
                if (start > 0)
                    memmove(buf_, buf_ + start, matchLen);
                fill_ = matchLen;
                return;
            }
        }

        fill_ = 0;
    }

    uint8_t  buf_[IMUD_PACKET_SIZE];   /* accumulation buffer */
    size_t   fill_;
    imud_packet_t pkt_;                /* last valid packet (copy) */
    uint32_t packetsReceived_;
    uint32_t crcErrors_;
    uint32_t resyncs_;
};

/* ─────────────────────────────────────────────────────────────────────────
 * ImudClient — thin Arduino wrapper. Only compiled under the Arduino
 * build (PlatformIO's "native" test environment doesn't define ARDUINO
 * and has no Arduino.h/Client.h/Udp.h on its include path, which is
 * exactly what lets ImudParser above be unit-tested with plain g++).
 * ───────────────────────────────────────────────────────────────────────*/

#ifdef ARDUINO

#include <Arduino.h>
#include <Client.h>
#include <Udp.h>

/* ImudClient owns no sockets — only pointers to caller-owned transports
 * (Client&/UDP&) — and never blocks except during an explicit or
 * throttled-automatic TCP reconnect (Client::connect() itself blocks,
 * sometimes for seconds on ESP32; see setAutoReconnect() below). */
class ImudClient {
public:
    ImudClient()
        : transport_(TRANSPORT_NONE), tcpClient_(nullptr), udpClient_(nullptr),
          useIP_(false), port_(0), autoReconnect_(true),
          lastReconnectAttempt_(0), lastPacketMillis_(0), everReceived_(false) {
        host_[0] = '\0';
    }

    /* TCP — client-owned Client instance (WiFiClient, EthernetClient, ...).
     * Stores host/port for reconnects. Returns the initial connect result;
     * a failed initial connect is fine, auto-reconnect takes over. */
    bool beginTCP(Client &c, const char *host, uint16_t port = 10112) {
        tcpClient_ = &c;
        udpClient_ = nullptr;
        transport_ = TRANSPORT_TCP;
        useIP_ = false;
        strncpy(host_, host, sizeof(host_) - 1);
        host_[sizeof(host_) - 1] = '\0';
        port_ = port;
        lastReconnectAttempt_ = millis();
        return tcpClient_->connect(host_, port_) != 0;
    }

    bool beginTCP(Client &c, IPAddress host, uint16_t port = 10112) {
        tcpClient_ = &c;
        udpClient_ = nullptr;
        transport_ = TRANSPORT_TCP;
        useIP_ = true;
        ip_ = host;
        port_ = port;
        lastReconnectAttempt_ = millis();
        return tcpClient_->connect(ip_, port_) != 0;
    }

    /* UDP — client-owned UDP instance. If alreadyBound is false, calls
     * u.begin(port) to bind (sufficient for unicast/broadcast reception).
     * For multicast, the abstract Arduino UDP class has no portable join
     * method: the caller joins first (e.g. WiFiUDP::beginMulticast on
     * ESP32/ESP8266) and passes alreadyBound=true. */
    bool beginUDP(UDP &u, uint16_t port = 10111, bool alreadyBound = false) {
        udpClient_ = &u;
        tcpClient_ = nullptr;
        transport_ = TRANSPORT_UDP;
        port_ = port;
        if (alreadyBound)
            return true;
        return udpClient_->begin(port) != 0;
    }

    /* Non-blocking. TCP: reads all available() bytes (through a small
     * stack chunk buffer) into the parser, and may trigger a throttled
     * auto-reconnect when disconnected. UDP: drains every pending
     * datagram. Returns true if at least one NEW valid packet arrived;
     * packet() then holds the newest one. */
    bool poll() {
        switch (transport_) {
            case TRANSPORT_TCP: return pollTCP();
            case TRANSPORT_UDP: return pollUDP();
            default: return false;
        }
    }

    const imud_packet_t &packet() const { return parser_.packet(); }

    /* §imud_true_heading() applied to the current packet. */
    float trueHeading() const { return imud_true_heading(&parser_.packet()); }

    bool connected() const {
        switch (transport_) {
            case TRANSPORT_TCP: return tcpClient_ && tcpClient_->connected();
            case TRANSPORT_UDP: return udpClient_ != nullptr;
            default: return false;
        }
    }

    /* TCP only: stop + connect to the stored host/port. Blocks like
     * Client::connect() does. Returns false (no-op) on UDP. */
    bool reconnect() {
        if (transport_ != TRANSPORT_TCP || !tcpClient_)
            return false;
        tcpClient_->stop();
        lastReconnectAttempt_ = millis();
        if (useIP_)
            return tcpClient_->connect(ip_, port_) != 0;
        return tcpClient_->connect(host_, port_) != 0;
    }

    /* Default on, minimum 2 s between attempts (millis()-based). TCP only;
     * ignored for UDP. Client::connect() blocks — sometimes for seconds on
     * ESP32 — so control loops that can't afford that should call
     * setAutoReconnect(false) and invoke reconnect() when convenient. */
    void setAutoReconnect(bool on) { autoReconnect_ = on; }

    /* Milliseconds since the last new valid packet, or UINT32_MAX before
     * the first one — handy for watchdogs/UI staleness indicators. */
    uint32_t millisSinceLastPacket() const {
        if (!everReceived_)
            return UINT32_MAX;
        return millis() - lastPacketMillis_;
    }

    /* True if the newest packet carried IMUD_FLAG_SHUTDOWN — the daemon's
     * final packet before a clean exit. Display "daemon stopping" and
     * suppress the reconnect-alarm UI on this, rather than treating it as
     * a dropped link. */
    bool daemonShutdown() const {
        return (parser_.packet().flags & IMUD_FLAG_SHUTDOWN) != 0;
    }

    uint32_t packetsReceived() const { return parser_.packetsReceived(); }
    uint32_t crcErrors() const { return parser_.crcErrors(); }
    uint32_t resyncs() const { return parser_.resyncs(); }

    /* Stop the transport and reset the parser (partial buffer + counters). */
    void end() {
        if (transport_ == TRANSPORT_TCP && tcpClient_) {
            tcpClient_->stop();
        } else if (transport_ == TRANSPORT_UDP && udpClient_) {
            udpClient_->stop();
        }
        transport_ = TRANSPORT_NONE;
        tcpClient_ = nullptr;
        udpClient_ = nullptr;
        parser_.reset();
    }

private:
    enum Transport : uint8_t { TRANSPORT_NONE, TRANSPORT_TCP, TRANSPORT_UDP };

    static const uint32_t RECONNECT_MIN_INTERVAL_MS = 2000;
    static const size_t TCP_CHUNK_SIZE = 64;

    bool pollTCP() {
        if (!tcpClient_)
            return false;

        if (!tcpClient_->connected()) {
            if (autoReconnect_ &&
                (uint32_t)(millis() - lastReconnectAttempt_) >= RECONNECT_MIN_INTERVAL_MS) {
                reconnect();
            }
            return false;
        }

        uint8_t chunk[TCP_CHUNK_SIZE];
        bool any = false;
        int avail;
        while ((avail = tcpClient_->available()) > 0) {
            size_t want = (size_t)avail < sizeof(chunk) ? (size_t)avail : sizeof(chunk);
            int n = tcpClient_->read(chunk, want);
            if (n <= 0)
                break;
            if (parser_.feed(chunk, (size_t)n) > 0) {
                any = true;
                lastPacketMillis_ = millis();
                everReceived_ = true;
            }
        }
        return any;
    }

    bool pollUDP() {
        if (!udpClient_)
            return false;

        bool any = false;
        int packetSize;
        while ((packetSize = udpClient_->parsePacket()) > 0) {
            if (packetSize == IMUD_PACKET_SIZE) {
                uint8_t chunk[IMUD_PACKET_SIZE];
                int n = udpClient_->read(chunk, sizeof(chunk));
                if (n == IMUD_PACKET_SIZE && parser_.feedDatagram(chunk, (size_t)n)) {
                    any = true;
                    lastPacketMillis_ = millis();
                    everReceived_ = true;
                }
            }
            /* Oversized/undersized datagram: left unread and dropped —
             * the next parsePacket() call discards any remainder. */
        }
        return any;
    }

    Transport transport_;
    Client *tcpClient_;
    UDP *udpClient_;
    ImudParser parser_;

    char host_[64];
    IPAddress ip_;
    bool useIP_;
    uint16_t port_;

    bool autoReconnect_;
    uint32_t lastReconnectAttempt_;

    uint32_t lastPacketMillis_;
    bool everReceived_;
};

#endif /* ARDUINO */

#endif /* IMUD_CLIENT_H */
