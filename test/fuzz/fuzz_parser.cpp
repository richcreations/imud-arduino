/*
 * fuzz_parser.cpp — libFuzzer target for ImudParser.
 *
 * ImudParser consumes untrusted bytes straight off a network socket, so it is
 * exactly the kind of code fuzzing is for. Upstream already found a real bug
 * this way (the Inf/NaN hardening in imud_true_heading exists because of it),
 * and ImudParser::resyncScan() is raw index arithmetic over a fixed buffer —
 * safe today, but the sort of invariant a later edit breaks silently.
 *
 * The seed corpus is the extras/golden hex vectors decoded to raw bytes.
 * That matters:
 * without seeds a fuzzer essentially never guesses a valid 32-bit CRC, so it
 * would only ever exercise the reject paths and would never reach the
 * accept path or the state that follows a successful decode.
 *
 * BUILD (CI does this; libFuzzer needs real clang — Apple clang does not
 * ship it, so on macOS use the replay mode below instead):
 *
 *   clang++ -std=c++14 -g -O1 -fsanitize=fuzzer,address,undefined \
 *           -Isrc -o fuzz_parser test/fuzz/fuzz_parser.cpp
 *   ./fuzz_parser corpus/ -max_total_time=60
 *
 * REPLAY MODE (no libFuzzer needed — works anywhere, for reproducing a
 * crash found in CI):
 *
 *   c++ -std=c++14 -g -O1 -fsanitize=address,undefined -DIMUD_FUZZ_REPLAY \
 *       -Isrc -o replay test/fuzz/fuzz_parser.cpp
 *   ./replay path/to/crash-input ...
 *
 * Copyright (c) 2026 Richard Simpson
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "ImudClient.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1)
        return 0;

    /* Byte 0 selects how the remaining bytes are split across feed() calls.
     * Chunking is a real dimension of this parser's state machine, not an
     * implementation detail: a magic sequence straddling two feed() calls
     * takes a different path through resyncScan() than one that doesn't, and
     * a real TCP stack can deliver bytes in any grouping. Letting the fuzzer
     * drive it means it explores those splits rather than always seeing
     * whole-buffer feeds. */
    const size_t chunkMax = (size_t)(data[0] & 0x7F) + 1;   /* 1..128 */
    const uint8_t *body = data + 1;
    const size_t bodyLen = size - 1;

    ImudParser p;

    size_t off = 0;
    size_t step = 1;
    while (off < bodyLen) {
        /* Vary the chunk size as we go so a single input covers several
         * split patterns instead of one uniform stride. */
        size_t n = (step % chunkMax) + 1;
        if (n > bodyLen - off)
            n = bodyLen - off;
        p.feed(body + off, n);
        off += n;
        step++;
    }

    /* The datagram path has different validation (exact size, no resync), so
     * exercise it too — including deliberately wrong lengths. */
    p.feedDatagram(body, bodyLen);
    if (bodyLen > IMUD_PACKET_SIZE)
        p.feedDatagram(body, IMUD_PACKET_SIZE);

    /* Whatever the parser accepted, the documented contract for
     * imud_true_heading() is: either the -1.0f sentinel, or a bearing in
     * [0, 360). Wire data is untrusted, so a crafted packet carrying
     * Inf/NaN/1e38 must not escape that range or hang the wraparound loop.
     * Asserting it here turns the fuzzer into a checker for the hardening,
     * not just a crash-finder. */
    const imud_packet_t &pkt = p.packet();
    float h = imud_true_heading(&pkt);
    if (!(h == -1.0f || (h >= 0.0f && h < 360.0f))) {
        __builtin_trap();
    }

    /* reset() must leave the parser reusable, not wedged. */
    p.reset();
    p.feed(body, bodyLen > 8 ? 8 : bodyLen);

    return 0;
}

#ifdef IMUD_FUZZ_REPLAY
/* Standalone driver: feed each file named on the command line through the
 * target once. Lets a crash from CI be reproduced without libFuzzer. */
#include <cstdio>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[i]);
            return 1;
        }
        static uint8_t buf[1 << 20];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        LLVMFuzzerTestOneInput(buf, n);
        printf("ok: %s (%zu bytes)\n", argv[i], n);
    }
    printf("replayed %d input(s), no crash\n", argc - 1);
    return 0;
}
#endif
