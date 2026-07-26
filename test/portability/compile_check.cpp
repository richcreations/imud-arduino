/*
 * compile_check.cpp — portability gate for src/ImudClient.h.
 *
 * The library is header-only and gets compiled by whatever toolchain the
 * user's board core happens to ship: different compilers, different C++
 * standards, and — always — a 32-bit target. CI builds this file across
 * that cross-product with -Werror, so a warning or a layout assumption that
 * only shows up under one combination fails the build instead of reaching a
 * user's board.
 *
 * This deliberately does NOT duplicate the Unity suite's behavioural
 * coverage. It answers a narrower question: does the header compile warning
 * -free everywhere, and is the wire layout identical on every target?
 *
 *   c++ -std=c++11 -Wall -Wextra -Wpedantic -Werror -Isrc \
 *       -o compile_check test/portability/compile_check.cpp
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#include <cstdio>
#include <cstring>
#include "ImudClient.h"

/* The wire layout must be byte-identical on every target this compiles for.
 * These are compile-time, so a 32-bit build that padded the struct
 * differently would fail here rather than silently decoding garbage. */
static_assert(sizeof(imud_packet_t) == 276, "packet must be 276 bytes");
static_assert(sizeof(imud_packet_t) == IMUD_PACKET_SIZE, "size constant drift");
static_assert(offsetof(imud_packet_t, crc32) == 272, "crc32 must sit at 272");
static_assert(offsetof(imud_packet_t, innov_weight) == 256, "v17 append offset");
static_assert(offsetof(imud_packet_t, innov_reject) == 260, "v17 append offset");
static_assert(offsetof(imud_packet_t, nis_accel) == 264, "v17 append offset");
static_assert(offsetof(imud_packet_t, nis_mag) == 268, "v17 append offset");
static_assert(sizeof(float) == 4, "wire format assumes 32-bit float");
static_assert(IMUD_VERSION == 17, "wire version drift");

int main() {
    /* Known-answer test for the CRC. "123456789" -> 0xCBF43926 is the
     * standard CRC-32/ISO-HDLC check value; if a target's shifts or integer
     * promotions misbehave, this catches it. */
    const char *check = "123456789";
    uint32_t crc = imud_crc32(reinterpret_cast<const uint8_t *>(check), 9);
    if (crc != 0xCBF43926u) {
        std::printf("FAIL: crc32 check value = 0x%08X, expected 0xCBF43926\n", crc);
        return 1;
    }

    /* Exercise the parser's public surface so every member is instantiated
     * and type-checked, not merely parsed. */
    ImudParser p;
    uint8_t junk[64];
    std::memset(junk, 0xA5, sizeof(junk));
    p.feed(junk, sizeof(junk));
    p.feedDatagram(junk, sizeof(junk));
    if (p.packetsReceived() != 0 || p.crcErrors() == 0) {
        std::printf("FAIL: junk input should be rejected, not accepted\n");
        return 1;
    }
    p.reset();

    /* The hardened heading path must return the sentinel for a zeroed
     * packet (no DECLINATION_VALID flag). */
    if (imud_true_heading(&p.packet()) != -1.0f) {
        std::printf("FAIL: true_heading should be -1.0 without declination\n");
        return 1;
    }

    if (imud_rad_to_deg(0.0f) != 0.0f) {
        std::printf("FAIL: rad_to_deg(0) should be 0\n");
        return 1;
    }

    std::printf("portability OK: %zu-bit, sizeof(imud_packet_t)=%zu, C++%ld\n",
                sizeof(void *) * 8, sizeof(imud_packet_t), (long)__cplusplus);
    return 0;
}
