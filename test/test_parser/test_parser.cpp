/*
 * test_parser.cpp — native unit tests for ImudParser
 *
 * Runs with `pio test -e native` (PlatformIO + Unity), no Arduino, no
 * hardware, no mocks — ImudParser has zero Arduino dependencies, so it
 * builds and runs directly on the host.
 *
 * The byte arrays below are exact transcriptions of the hex files under
 * extras/golden/ (lowercase hex, 32 bytes/line, LF endings — strip whitespace and
 * hex-decode to reproduce them), generated with:
 *
 *   python3 -c "
 *   data = ''.join(open('extras/golden/valid_packet.hex').read().split())
 *   b = bytes.fromhex(data)
 *   print(', '.join(f'0x{x:02x}' for x in b))"
 *
 * Expected values are documented in extras/golden/valid_packet.md.
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#include <unity.h>
#include <cstring>
#include <vector>
#include "ImudClient.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Golden vectors (extras/golden/, transcribed verbatim)
 * ───────────────────────────────────────────────────────────────────────*/

static const uint8_t kValidPacket[260] = {
    0x44, 0x55, 0x4d, 0x49, 0x0e, 0x00, 0x3d, 0x1c, 0x15, 0x4d, 0x9e, 0x5a,
    0x36, 0xe8, 0x53, 0x18, 0x15, 0x7f, 0xfd, 0xf7, 0x3e, 0xe8, 0x53, 0x18,
    0x80, 0x1a, 0x06, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e,
    0x00, 0x00, 0x80, 0xbe, 0x00, 0x00, 0x1d, 0xc1, 0x00, 0x00, 0x20, 0x3e,
    0x00, 0x00, 0x90, 0xbe, 0x00, 0x80, 0x1d, 0xc1, 0x00, 0x00, 0x40, 0x3c,
    0x00, 0x00, 0xc0, 0xbc, 0x00, 0x00, 0x40, 0x3d, 0x00, 0x00, 0x80, 0x3c,
    0x00, 0x00, 0xa0, 0xbc, 0x00, 0x00, 0x50, 0x3d, 0x00, 0x00, 0xac, 0x41,
    0x00, 0x00, 0xa8, 0xc0, 0x00, 0x00, 0x2f, 0x42, 0x00, 0x00, 0xa0, 0x41,
    0x00, 0x00, 0xd0, 0xc0, 0x00, 0x00, 0x32, 0x42, 0x00, 0x00, 0x78, 0x3f,
    0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x40, 0xbe, 0x00, 0x00, 0xc0, 0x3d,
    0x00, 0x00, 0x80, 0x3d, 0x00, 0x00, 0x00, 0xbe, 0x00, 0x00, 0xc0, 0x3f,
    0x00, 0x00, 0xf7, 0x42, 0x00, 0x00, 0x90, 0xc0, 0x00, 0x00, 0x12, 0x42,
    0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3b,
    0x32, 0x79, 0x06, 0x00, 0x00, 0x00, 0x34, 0x41, 0x00, 0x00, 0xc0, 0x3e,
    0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x00, 0x3c,
    0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x3b,
    0x00, 0x00, 0x80, 0xbd, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 0xa0, 0x3f,
    0x00, 0x00, 0x90, 0x40, 0x00, 0x00, 0xd0, 0x40, 0x00, 0x00, 0xc0, 0x3d,
    0x00, 0x00, 0xa8, 0x40, 0x00, 0x00, 0x40, 0x3d, 0x00, 0x00, 0x80, 0x3c,
    0x00, 0x00, 0x00, 0x3c, 0xe2, 0x45, 0xe5, 0xf9,
};

static const uint8_t kBadCrcPacket[260] = {
    0x44, 0x55, 0x4d, 0x49, 0x0e, 0x00, 0x3d, 0x1c, 0x15, 0x4d, 0x9e, 0x5a,
    0x36, 0xe8, 0x53, 0x18, 0x15, 0x7f, 0xfd, 0xf7, 0x3e, 0xe8, 0x53, 0x18,
    0x80, 0x1a, 0x06, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e,
    0x00, 0x00, 0x80, 0xbe, 0x00, 0x00, 0x1d, 0xc1, 0x00, 0x00, 0x20, 0x3e,
    0x00, 0x00, 0x90, 0xbe, 0x00, 0x80, 0x1d, 0xc1, 0x00, 0x00, 0x40, 0x3c,
    0x00, 0x00, 0xc0, 0xbc, 0x00, 0x00, 0x40, 0x3d, 0x00, 0x00, 0x80, 0x3c,
    0x00, 0x00, 0xa0, 0xbc, 0x00, 0x00, 0x50, 0x3d, 0x00, 0x00, 0xac, 0x41,
    0x00, 0x00, 0xa8, 0xc0, 0x00, 0x00, 0x2f, 0x42, 0x00, 0x00, 0xa0, 0x41,
    0x00, 0x00, 0xd0, 0xc0, 0x00, 0x00, 0x32, 0x42, 0x00, 0x00, 0x78, 0x3f,
    0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x40, 0xbe, 0x00, 0x00, 0xc0, 0x3d,
    0x00, 0x00, 0x80, 0x3d, 0x00, 0x00, 0x00, 0xbe, 0x00, 0x00, 0xc0, 0x3f,
    0x00, 0x00, 0xf7, 0x42, 0x00, 0x00, 0x90, 0xc0, 0x00, 0x00, 0x12, 0x42,
    0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3b,
    0x32, 0x79, 0x06, 0x00, 0x00, 0x00, 0x34, 0x41, 0x00, 0x00, 0xc0, 0x3e,
    0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x00, 0x3c,
    0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x3b,
    0x00, 0x00, 0x80, 0xbd, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 0xa0, 0x3f,
    0x00, 0x00, 0x90, 0x40, 0x00, 0x00, 0xd0, 0x40, 0x00, 0x00, 0xc0, 0x3d,
    0x00, 0x00, 0xa8, 0x40, 0x00, 0x00, 0x40, 0x3d, 0x00, 0x00, 0x80, 0x3c,
    0x00, 0x00, 0x00, 0x3c, 0xe2, 0x45, 0xe5, 0x06,
};

static const uint8_t kResyncStream[541] = {
    0x00, 0xff, 0x4e, 0x4f, 0x49, 0x53, 0x45, 0x44, 0x55, 0x4d, 0x49, 0x0e,
    0x00, 0x67, 0x61, 0x72, 0x62, 0x61, 0x67, 0x65, 0x21, 0x44, 0x55, 0x4d,
    0x49, 0x0e, 0x00, 0x3d, 0x1c, 0x15, 0x4d, 0x9e, 0x5a, 0x36, 0xe8, 0x53,
    0x18, 0x15, 0x7f, 0xfd, 0xf7, 0x3e, 0xe8, 0x53, 0x18, 0x80, 0x1a, 0x06,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x80,
    0xbe, 0x00, 0x00, 0x1d, 0xc1, 0x00, 0x00, 0x20, 0x3e, 0x00, 0x00, 0x90,
    0xbe, 0x00, 0x80, 0x1d, 0xc1, 0x00, 0x00, 0x40, 0x3c, 0x00, 0x00, 0xc0,
    0xbc, 0x00, 0x00, 0x40, 0x3d, 0x00, 0x00, 0x80, 0x3c, 0x00, 0x00, 0xa0,
    0xbc, 0x00, 0x00, 0x50, 0x3d, 0x00, 0x00, 0xac, 0x41, 0x00, 0x00, 0xa8,
    0xc0, 0x00, 0x00, 0x2f, 0x42, 0x00, 0x00, 0xa0, 0x41, 0x00, 0x00, 0xd0,
    0xc0, 0x00, 0x00, 0x32, 0x42, 0x00, 0x00, 0x78, 0x3f, 0x00, 0x00, 0x00,
    0x3e, 0x00, 0x00, 0x40, 0xbe, 0x00, 0x00, 0xc0, 0x3d, 0x00, 0x00, 0x80,
    0x3d, 0x00, 0x00, 0x00, 0xbe, 0x00, 0x00, 0xc0, 0x3f, 0x00, 0x00, 0xb5,
    0x42, 0x00, 0x00, 0x90, 0xc0, 0x00, 0x00, 0x12, 0x42, 0x00, 0x00, 0x80,
    0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3b, 0xe8, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x34, 0x41, 0x00, 0x00, 0xc0, 0x3e, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x80,
    0x3a, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x80,
    0xbd, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 0xa0, 0x3f, 0x00, 0x00, 0x90,
    0x40, 0x00, 0x00, 0xd0, 0x40, 0x00, 0x00, 0xc0, 0x3d, 0x00, 0x00, 0xa8,
    0x40, 0x00, 0x00, 0x40, 0x3d, 0x00, 0x00, 0x80, 0x3c, 0x00, 0x00, 0x00,
    0x3c, 0x90, 0x51, 0xb6, 0xaa, 0x44, 0x55, 0x4d, 0x49, 0x0e, 0x00, 0x3d,
    0x1c, 0x95, 0xe3, 0x36, 0x5b, 0x36, 0xe8, 0x53, 0x18, 0x15, 0x7f, 0xfd,
    0xf7, 0x3e, 0xe8, 0x53, 0x18, 0x80, 0x1a, 0x06, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x80, 0xbe, 0x00, 0x00, 0x1d,
    0xc1, 0x00, 0x00, 0x20, 0x3e, 0x00, 0x00, 0x90, 0xbe, 0x00, 0x80, 0x1d,
    0xc1, 0x00, 0x00, 0x40, 0x3c, 0x00, 0x00, 0xc0, 0xbc, 0x00, 0x00, 0x40,
    0x3d, 0x00, 0x00, 0x80, 0x3c, 0x00, 0x00, 0xa0, 0xbc, 0x00, 0x00, 0x50,
    0x3d, 0x00, 0x00, 0xac, 0x41, 0x00, 0x00, 0xa8, 0xc0, 0x00, 0x00, 0x2f,
    0x42, 0x00, 0x00, 0xa0, 0x41, 0x00, 0x00, 0xd0, 0xc0, 0x00, 0x00, 0x32,
    0x42, 0x00, 0x00, 0x78, 0x3f, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x40,
    0xbe, 0x00, 0x00, 0xc0, 0x3d, 0x00, 0x00, 0x80, 0x3d, 0x00, 0x00, 0x00,
    0xbe, 0x00, 0x00, 0xc0, 0x3f, 0x00, 0x00, 0xb7, 0x42, 0x00, 0x00, 0x90,
    0xc0, 0x00, 0x00, 0x12, 0x42, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x3b, 0xe9, 0x03, 0x00, 0x00, 0x00, 0x00, 0x34,
    0x41, 0x00, 0x00, 0xc0, 0x3e, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x80,
    0xbb, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x80, 0x3a, 0x00, 0x00, 0x80,
    0x3a, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x80, 0xbd, 0x00, 0x00, 0x00,
    0x3d, 0x00, 0x00, 0xa0, 0x3f, 0x00, 0x00, 0x90, 0x40, 0x00, 0x00, 0xd0,
    0x40, 0x00, 0x00, 0xc0, 0x3d, 0x00, 0x00, 0xa8, 0x40, 0x00, 0x00, 0x40,
    0x3d, 0x00, 0x00, 0x80, 0x3c, 0x00, 0x00, 0x00, 0x3c, 0x10, 0xae, 0x95,
    0x78,
};

/* ─────────────────────────────────────────────────────────────────────────
 * Helpers
 * ───────────────────────────────────────────────────────────────────────*/

/* Feeds `data` in fixed-size chunks (last chunk may be short). */
static size_t feedChunked(ImudParser &p, const uint8_t *data, size_t len,
                           size_t chunkSize) {
    size_t total = 0;
    for (size_t i = 0; i < len; i += chunkSize) {
        size_t n = (len - i) < chunkSize ? (len - i) : chunkSize;
        total += p.feed(data + i, n);
    }
    return total;
}

static void assertFloatField(float expected, float actual) {
    TEST_ASSERT_EQUAL_FLOAT(expected, actual);
}

/* Checks every field of a decoded valid_packet against the exact values
 * documented in extras/golden/valid_packet.md. */
static void assertValidPacketFields(const imud_packet_t &p) {
    TEST_ASSERT_EQUAL_UINT32(1229804868u, p.magic);
    TEST_ASSERT_EQUAL_UINT32(IMUD_MAGIC, p.magic);
    TEST_ASSERT_EQUAL_UINT16(14, p.version);
    TEST_ASSERT_EQUAL_UINT16(7229, p.flags);
    TEST_ASSERT_EQUAL_UINT64(1753000000123456789ULL, p.ts_wall_ns);
    TEST_ASSERT_EQUAL_UINT64(1753000037123456789ULL, p.ts_tai_ns);
    TEST_ASSERT_EQUAL_UINT32(400000, p.ts_chip_ticks);
    TEST_ASSERT_EQUAL_UINT32(3, p.anchor_gen);

    assertFloatField(0.125f, p.accel_x);
    assertFloatField(-0.25f, p.accel_y);
    assertFloatField(-9.8125f, p.accel_z);
    assertFloatField(0.15625f, p.accel_raw_x);
    assertFloatField(-0.28125f, p.accel_raw_y);
    assertFloatField(-9.84375f, p.accel_raw_z);

    assertFloatField(0.01171875f, p.gyro_x);
    assertFloatField(-0.0234375f, p.gyro_y);
    assertFloatField(0.046875f, p.gyro_z);
    assertFloatField(0.015625f, p.gyro_raw_x);
    assertFloatField(-0.01953125f, p.gyro_raw_y);
    assertFloatField(0.05078125f, p.gyro_raw_z);

    assertFloatField(21.5f, p.mag_x);
    assertFloatField(-5.25f, p.mag_y);
    assertFloatField(43.75f, p.mag_z);
    assertFloatField(20.0f, p.mag_raw_x);
    assertFloatField(-6.5f, p.mag_raw_y);
    assertFloatField(44.5f, p.mag_raw_z);

    assertFloatField(0.96875f, p.quat_w);
    assertFloatField(0.125f, p.quat_x);
    assertFloatField(-0.1875f, p.quat_y);
    assertFloatField(0.09375f, p.quat_z);
    assertFloatField(0.0625f, p.pitch);
    assertFloatField(-0.125f, p.roll);
    assertFloatField(1.5f, p.yaw);
    assertFloatField(123.5f, p.heading_deg);
    assertFloatField(-4.5f, p.rate_of_turn);
    assertFloatField(36.5f, p.temp_c);

    assertFloatField(0.0009765625f, p.cov[0]);
    assertFloatField(0.0f, p.cov[1]);
    assertFloatField(0.0f, p.cov[2]);
    assertFloatField(0.0f, p.cov[3]);
    assertFloatField(0.0009765625f, p.cov[4]);
    assertFloatField(0.0f, p.cov[5]);
    assertFloatField(0.0f, p.cov[6]);
    assertFloatField(0.0f, p.cov[7]);
    assertFloatField(0.00390625f, p.cov[8]);

    TEST_ASSERT_EQUAL_UINT32(424242, p.imu_seq);
    assertFloatField(11.25f, p.declination_deg);
    assertFloatField(0.375f, p.heave_m);

    assertFloatField(0.001953125f, p.gyro_bias_x);
    assertFloatField(-0.00390625f, p.gyro_bias_y);
    assertFloatField(0.0078125f, p.gyro_bias_z);
    assertFloatField(0.0009765625f, p.gyro_bias_var_x);
    assertFloatField(0.0009765625f, p.gyro_bias_var_y);
    assertFloatField(0.001953125f, p.gyro_bias_var_z);
    assertFloatField(-0.0625f, p.heave_rate);
    assertFloatField(0.03125f, p.accel_quiescence);

    assertFloatField(1.25f, p.wave_height_m);
    assertFloatField(4.5f, p.wave_period_s);
    assertFloatField(6.5f, p.roll_period_s);
    assertFloatField(0.09375f, p.roll_amplitude);
    assertFloatField(5.25f, p.pitch_period_s);
    assertFloatField(0.046875f, p.pitch_amplitude);
    assertFloatField(0.015625f, p.mag_anomaly);
    assertFloatField(0.0078125f, p.mag_residual);

    TEST_ASSERT_EQUAL_UINT32(4192552418u, p.crc32);
    TEST_ASSERT_EQUAL_UINT32(0xF9E545E2u, p.crc32);

    /* Flag breakdown: MAG_VALID | FUSION_CONVERGED | ACCEL_CAL | GYRO_CAL |
     * MAG_CAL | DECLINATION_VALID | HEAVE_VALID | WAVE_VALID == 0x1C3D */
    uint16_t expectedFlags = IMUD_FLAG_MAG_VALID | IMUD_FLAG_FUSION_CONVERGED |
                              IMUD_FLAG_ACCEL_CAL | IMUD_FLAG_GYRO_CAL |
                              IMUD_FLAG_MAG_CAL | IMUD_FLAG_DECLINATION_VALID |
                              IMUD_FLAG_HEAVE_VALID | IMUD_FLAG_WAVE_VALID;
    TEST_ASSERT_EQUAL_HEX16(0x1C3D, expectedFlags);
    TEST_ASSERT_EQUAL_UINT16(expectedFlags, p.flags);
}

/* ─────────────────────────────────────────────────────────────────────────
 * §7.1 — valid_packet fed whole
 * ───────────────────────────────────────────────────────────────────────*/

static void test_valid_packet_whole(void) {
    ImudParser p;
    size_t got = p.feed(kValidPacket, sizeof(kValidPacket));

    TEST_ASSERT_EQUAL_UINT32(1, got);
    TEST_ASSERT_EQUAL_UINT32(1, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(0, p.crcErrors());
    TEST_ASSERT_EQUAL_UINT32(0, p.resyncs());

    assertValidPacketFields(p.packet());
    TEST_ASSERT_EQUAL_FLOAT(134.75f, imud_true_heading(&p.packet()));
}

/* ─────────────────────────────────────────────────────────────────────────
 * §7.2 — valid_packet fed 1 byte at a time, and chunked 7 bytes at a time
 * ───────────────────────────────────────────────────────────────────────*/

static void test_valid_packet_one_byte_at_a_time(void) {
    ImudParser p;
    size_t got = feedChunked(p, kValidPacket, sizeof(kValidPacket), 1);

    TEST_ASSERT_EQUAL_UINT32(1, got);
    TEST_ASSERT_EQUAL_UINT32(1, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(0, p.crcErrors());
    assertValidPacketFields(p.packet());
    TEST_ASSERT_EQUAL_FLOAT(134.75f, imud_true_heading(&p.packet()));
}

static void test_valid_packet_seven_bytes_at_a_time(void) {
    ImudParser p;
    size_t got = feedChunked(p, kValidPacket, sizeof(kValidPacket), 7);

    TEST_ASSERT_EQUAL_UINT32(1, got);
    TEST_ASSERT_EQUAL_UINT32(1, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(0, p.crcErrors());
    assertValidPacketFields(p.packet());
    TEST_ASSERT_EQUAL_FLOAT(134.75f, imud_true_heading(&p.packet()));
}

/* ─────────────────────────────────────────────────────────────────────────
 * §7.3 — bad_crc_packet must be rejected
 * ───────────────────────────────────────────────────────────────────────*/

static void test_bad_crc_packet_rejected(void) {
    ImudParser p;
    size_t got = p.feed(kBadCrcPacket, sizeof(kBadCrcPacket));

    TEST_ASSERT_EQUAL_UINT32(0, got);
    TEST_ASSERT_EQUAL_UINT32(0, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(1, p.crcErrors());
}

/* ─────────────────────────────────────────────────────────────────────────
 * §7.4 — version mismatch must be rejected even with a correct CRC
 * ───────────────────────────────────────────────────────────────────────*/

static void test_version_mismatch_rejected(void) {
    uint8_t pkt[IMUD_PACKET_SIZE];
    memcpy(pkt, kValidPacket, sizeof(pkt));

    pkt[4] = 13;  /* version LSB: 14 -> 13, an otherwise-valid layout */

    /* Rebuild the CRC over the modified body so the CRC check alone can't
     * explain a rejection — this isolates the version check. */
    uint32_t crc = imud_crc32(pkt, offsetof(imud_packet_t, crc32));
    memcpy(pkt + offsetof(imud_packet_t, crc32), &crc, sizeof(crc));

    ImudParser p;
    size_t got = p.feed(pkt, sizeof(pkt));

    TEST_ASSERT_EQUAL_UINT32(0, got);
    TEST_ASSERT_EQUAL_UINT32(0, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(1, p.crcErrors());
}

/* ─────────────────────────────────────────────────────────────────────────
 * §7.5 — resync_stream: exactly 2 packets, seq 1000 then 1001, whole and
 * one byte at a time
 * ───────────────────────────────────────────────────────────────────────*/

static void test_resync_stream_whole(void) {
    ImudParser p;
    std::vector<uint32_t> seqs;
    size_t total = 0;
    size_t got = p.feed(kResyncStream, sizeof(kResyncStream));
    /* feed() only reports the count of new packets in this call and
     * leaves packet() holding the newest; re-run byte-wise below to also
     * capture each intermediate packet's seq without re-architecting the
     * parser just for the test. */
    total += got;

    TEST_ASSERT_EQUAL_UINT32(2, total);
    TEST_ASSERT_EQUAL_UINT32(1001, p.packet().imu_seq);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, p.resyncs());
    (void)seqs;
}

static void test_resync_stream_one_byte_at_a_time(void) {
    ImudParser p;
    uint32_t seqs[4] = {0, 0, 0, 0};
    size_t nseq = 0;
    size_t total = 0;

    for (size_t i = 0; i < sizeof(kResyncStream); i++) {
        size_t got = p.feed(&kResyncStream[i], 1);
        if (got > 0) {
            total += got;
            if (nseq < 4)
                seqs[nseq++] = p.packet().imu_seq;
        }
    }

    TEST_ASSERT_EQUAL_UINT32(2, total);
    TEST_ASSERT_EQUAL_UINT32(2, nseq);
    TEST_ASSERT_EQUAL_UINT32(1000, seqs[0]);
    TEST_ASSERT_EQUAL_UINT32(1001, seqs[1]);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, p.resyncs());
}

static void test_resync_stream_seven_bytes_at_a_time(void) {
    ImudParser p;
    uint32_t seqs[4] = {0, 0, 0, 0};
    size_t nseq = 0;
    size_t total = 0;

    for (size_t i = 0; i < sizeof(kResyncStream); i += 7) {
        size_t n = (sizeof(kResyncStream) - i) < 7 ? (sizeof(kResyncStream) - i) : 7;
        size_t got = p.feed(&kResyncStream[i], n);
        if (got > 0) {
            total += got;
            if (nseq < 4)
                seqs[nseq++] = p.packet().imu_seq;
        }
    }

    TEST_ASSERT_EQUAL_UINT32(2, total);
    TEST_ASSERT_EQUAL_UINT32(2, nseq);
    TEST_ASSERT_EQUAL_UINT32(1000, seqs[0]);
    TEST_ASSERT_EQUAL_UINT32(1001, seqs[1]);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, p.resyncs());
}

/* ─────────────────────────────────────────────────────────────────────────
 * §7.6 — feedDatagram: exact size required
 * ───────────────────────────────────────────────────────────────────────*/

static void test_feed_datagram_exact_size_accepted(void) {
    ImudParser p;
    bool ok = p.feedDatagram(kValidPacket, sizeof(kValidPacket));

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(1, p.packetsReceived());
    assertValidPacketFields(p.packet());
}

static void test_feed_datagram_undersized_rejected(void) {
    ImudParser p;
    bool ok = p.feedDatagram(kValidPacket, sizeof(kValidPacket) - 1);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(1, p.crcErrors());
}

static void test_feed_datagram_oversized_rejected(void) {
    uint8_t oversized[IMUD_PACKET_SIZE + 1];
    memcpy(oversized, kValidPacket, sizeof(kValidPacket));
    oversized[IMUD_PACKET_SIZE] = 0x00;

    ImudParser p;
    bool ok = p.feedDatagram(oversized, sizeof(oversized));

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(1, p.crcErrors());
}

/* ─────────────────────────────────────────────────────────────────────────
 * Extra coverage: reset(), stale-buffer resilience, struct/CRC sanity
 * ───────────────────────────────────────────────────────────────────────*/

static void test_reset_clears_buffer_and_counters_not_packet(void) {
    ImudParser p;
    p.feed(kValidPacket, sizeof(kValidPacket));
    TEST_ASSERT_EQUAL_UINT32(1, p.packetsReceived());

    /* Leave a partial frame in the accumulation buffer. */
    p.feed(kValidPacket, 10);

    p.reset();

    TEST_ASSERT_EQUAL_UINT32(0, p.packetsReceived());
    TEST_ASSERT_EQUAL_UINT32(0, p.crcErrors());
    TEST_ASSERT_EQUAL_UINT32(0, p.resyncs());
    /* Last decoded packet is retained across reset(). */
    TEST_ASSERT_EQUAL_UINT32(424242, p.packet().imu_seq);

    /* The partial frame left before reset() must not leak into the next
     * valid frame's alignment. */
    size_t got = p.feed(kValidPacket, sizeof(kValidPacket));
    TEST_ASSERT_EQUAL_UINT32(1, got);
}

static void test_fresh_parser_packet_is_zeroed(void) {
    ImudParser p;
    TEST_ASSERT_EQUAL_UINT32(0, p.packet().magic);
    TEST_ASSERT_EQUAL_UINT16(0, p.packet().flags);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, imud_true_heading(&p.packet()));
}

static void test_struct_size_and_crc_offset(void) {
    TEST_ASSERT_EQUAL_UINT32(260, sizeof(imud_packet_t));
    TEST_ASSERT_EQUAL_UINT32(IMUD_PACKET_SIZE, sizeof(imud_packet_t));
    TEST_ASSERT_EQUAL_UINT32(256, offsetof(imud_packet_t, crc32));
}

static void test_true_heading_hardening_rejects_extreme_values(void) {
    imud_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.flags = IMUD_FLAG_DECLINATION_VALID;

    pkt.heading_deg = 1e38f;
    pkt.declination_deg = 1e38f;
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, imud_true_heading(&pkt));

    /* NaN must fail the range check (comparisons with NaN are false). */
    float nan = 0.0f / 0.0f;
    pkt.heading_deg = nan;
    pkt.declination_deg = 0.0f;
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, imud_true_heading(&pkt));

    /* Declination not valid -> sentinel regardless of heading. */
    pkt.flags = 0;
    pkt.heading_deg = 10.0f;
    pkt.declination_deg = 5.0f;
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, imud_true_heading(&pkt));

    /* Normal wraparound case. */
    pkt.flags = IMUD_FLAG_DECLINATION_VALID;
    pkt.heading_deg = 350.0f;
    pkt.declination_deg = 20.0f;
    TEST_ASSERT_EQUAL_FLOAT(10.0f, imud_true_heading(&pkt));
}

/* ─────────────────────────────────────────────────────────────────────────
 * Unity boilerplate
 * ───────────────────────────────────────────────────────────────────────*/

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_struct_size_and_crc_offset);
    RUN_TEST(test_fresh_parser_packet_is_zeroed);

    RUN_TEST(test_valid_packet_whole);
    RUN_TEST(test_valid_packet_one_byte_at_a_time);
    RUN_TEST(test_valid_packet_seven_bytes_at_a_time);

    RUN_TEST(test_bad_crc_packet_rejected);
    RUN_TEST(test_version_mismatch_rejected);

    RUN_TEST(test_resync_stream_whole);
    RUN_TEST(test_resync_stream_one_byte_at_a_time);
    RUN_TEST(test_resync_stream_seven_bytes_at_a_time);

    RUN_TEST(test_feed_datagram_exact_size_accepted);
    RUN_TEST(test_feed_datagram_undersized_rejected);
    RUN_TEST(test_feed_datagram_oversized_rejected);

    RUN_TEST(test_reset_clears_buffer_and_counters_not_packet);
    RUN_TEST(test_true_heading_hardening_rejects_extreme_values);

    return UNITY_END();
}
