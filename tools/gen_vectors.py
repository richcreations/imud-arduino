#!/usr/bin/env python3
#
# imud — golden wire-vector generator for imud-arduino
# Copyright (c) 2026 Richard Simpson
# SPDX-License-Identifier: MIT
#
"""Generate and self-verify imud-arduino's golden test vectors.

Run this from an imud checkout; it imports the daemon's own reference
Python client so the vectors cannot drift from the daemon:

    python3 gen_vectors.py --imud ~/Desktop/imud --out golden/

Outputs into --out:

    valid_packet.hex     one valid wire packet
    bad_crc_packet.hex   the same packet with the CRC's top byte flipped
    resync_stream.hex    garbage + decoy magic, then two valid frames
    valid_packet.md      the exact decoded field values, for the .md in
                         imud-arduino/extras/golden/
    c_arrays.txt         the same three vectors as C uint8_t arrays, ready
                         to paste into test/test_parser/test_parser.cpp

Every vector is verified by round-tripping through the reference client's
_parse() before it is written: valid packets must decode to exactly the
values below, and bad_crc_packet must be REJECTED. Nothing is written if a
check fails.

Field values are deliberately exact binary fractions (n/2^k), so a decoder
can be compared with == after float32 decode with no epsilon. They carry
over unchanged from the v14 vectors so a reviewer can diff old against new
and see only the four appended fields and the CRCs move.

To regenerate for a future wire version: update FIELDS below to match
include/types.h, and the reference client will refuse to parse if you get
it wrong.
"""

import argparse
import pathlib
import struct
import sys
import zlib

# ── Field values ─────────────────────────────────────────────────────────────
#
# Order MUST match include/types.h's imu_packet_t. The v14 values are
# unchanged from imud-arduino 1.0.0's vectors; the four v17 fields are new.

FIELDS = [
    # (name, struct code, value)
    ("magic",            "I", 0x494D5544),
    ("version",          "H", 17),
    ("flags",            "H", 0x1C3D),
    ("ts_wall_ns",       "Q", 1753000000123456789),
    ("ts_tai_ns",        "Q", 1753000037123456789),
    ("ts_chip_ticks",    "I", 400000),
    ("anchor_gen",       "I", 3),
    ("accel_x",          "f", 0.125),
    ("accel_y",          "f", -0.25),
    ("accel_z",          "f", -9.8125),
    ("accel_raw_x",      "f", 0.15625),
    ("accel_raw_y",      "f", -0.28125),
    ("accel_raw_z",      "f", -9.84375),
    ("gyro_x",           "f", 0.01171875),
    ("gyro_y",           "f", -0.0234375),
    ("gyro_z",           "f", 0.046875),
    ("gyro_raw_x",       "f", 0.015625),
    ("gyro_raw_y",       "f", -0.01953125),
    ("gyro_raw_z",       "f", 0.05078125),
    ("mag_x",            "f", 21.5),
    ("mag_y",            "f", -5.25),
    ("mag_z",            "f", 43.75),
    ("mag_raw_x",        "f", 20.0),
    ("mag_raw_y",        "f", -6.5),
    ("mag_raw_z",        "f", 44.5),
    ("quat_w",           "f", 0.96875),
    ("quat_x",           "f", 0.125),
    ("quat_y",           "f", -0.1875),
    ("quat_z",           "f", 0.09375),
    ("pitch",            "f", 0.0625),
    ("roll",             "f", -0.125),
    ("yaw",              "f", 1.5),
    ("heading_deg",      "f", 123.5),
    ("rate_of_turn",     "f", -4.5),
    ("temp_c",           "f", 36.5),
    ("cov_0",            "f", 0.0009765625),
    ("cov_1",            "f", 0.0),
    ("cov_2",            "f", 0.0),
    ("cov_3",            "f", 0.0),
    ("cov_4",            "f", 0.0009765625),
    ("cov_5",            "f", 0.0),
    ("cov_6",            "f", 0.0),
    ("cov_7",            "f", 0.0),
    ("cov_8",            "f", 0.00390625),
    ("imu_seq",          "I", 424242),
    ("declination_deg",  "f", 11.25),
    ("heave_m",          "f", 0.375),
    ("gyro_bias_x",      "f", 0.001953125),
    ("gyro_bias_y",      "f", -0.00390625),
    ("gyro_bias_z",      "f", 0.0078125),
    ("gyro_bias_var_x",  "f", 0.0009765625),
    ("gyro_bias_var_y",  "f", 0.0009765625),
    ("gyro_bias_var_z",  "f", 0.001953125),
    ("heave_rate",       "f", -0.0625),
    ("accel_quiescence", "f", 0.03125),
    ("wave_height_m",    "f", 1.25),
    ("wave_period_s",    "f", 4.5),
    ("roll_period_s",    "f", 6.5),
    ("roll_amplitude",   "f", 0.09375),
    ("pitch_period_s",   "f", 5.25),
    ("pitch_amplitude",  "f", 0.046875),
    ("mag_anomaly",      "f", 0.015625),
    ("mag_residual",     "f", 0.0078125),
    # ── v17 additions — MEKF update-gate health and NIS consistency ──────────
    ("innov_weight",     "f", 1.0),        # 1.0 = never capped
    ("innov_reject",     "f", 0.0),        # 0.0 = nothing rejected
    ("nis_accel",        "f", 0.9375),     # near 1.0 = covariance consistent
    ("nis_mag",          "f", 1.0625),     # slightly over 1 = mildly over-confident
]

PACKET_SIZE = 276
CRC_OFFSET  = 272

# Decoy magic planted in the resync stream: "IMUD" little-endian is
# 44 55 4d 49 in wire order. A naive parser locks onto it and must recover.
GARBAGE = bytes([0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22,
                 0x44, 0x55, 0x4D, 0x49,       # decoy magic
                 0x11, 0x00,                   # plausible version (17)
                 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22])


def build(overrides=None):
    """Pack one packet, returning (bytes, {name: value})."""
    vals = dict((n, v) for n, _, v in FIELDS)
    if overrides:
        vals.update(overrides)
    fmt = "<" + "".join(c for _, c, _ in FIELDS)
    body = struct.pack(fmt, *[vals[n] for n, _, _ in FIELDS])
    assert len(body) == CRC_OFFSET, f"body is {len(body)}, expected {CRC_OFFSET}"
    crc = zlib.crc32(body) & 0xFFFFFFFF
    vals["crc32"] = crc
    pkt = body + struct.pack("<I", crc)
    assert len(pkt) == PACKET_SIZE
    return pkt, vals


def to_hex(data):
    """Lowercase hex, 32 bytes (64 chars) per line, LF endings."""
    return "".join(data[i:i + 32].hex() + "\n" for i in range(0, len(data), 32))


def to_c_array(name, data):
    out = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 12):
        row = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        out.append(f"    {row},")
    out.append("};")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--imud", default=str(pathlib.Path(__file__).resolve().parent),
                    help="path to an imud checkout (needs lib/imud_client.py)")
    ap.add_argument("--out", default="golden", help="output directory")
    args = ap.parse_args()

    # The reference parser is the authority; import it from the imud tree.
    lib = pathlib.Path(args.imud).expanduser() / "lib"
    if not (lib / "imud_client.py").is_file():
        sys.exit(f"error: {lib}/imud_client.py not found — pass --imud <checkout>")
    sys.path.insert(0, str(lib))
    import imud_client as ref

    if ref.IMUD_PACKET_SIZE != PACKET_SIZE or ref.IMUD_VERSION != 17:
        sys.exit(f"error: reference client is v{ref.IMUD_VERSION}/"
                 f"{ref.IMUD_PACKET_SIZE}B, this generator targets v17/{PACKET_SIZE}B")

    outdir = pathlib.Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)

    # ── valid_packet ────────────────────────────────────────────────────────
    valid, vals = build()
    p = ref._parse(valid)
    if p is None:
        sys.exit("error: reference parser REJECTED the valid packet")
    for name, code, want in FIELDS:
        if code != "f":
            continue
        got = getattr(p, name, None)
        if got is None:            # cov_N is exposed as a tuple
            continue
        if got != want:
            sys.exit(f"error: {name} round-tripped as {got!r}, expected {want!r}")
    if tuple(p.cov) != tuple(v for n, _, v in FIELDS if n.startswith("cov_")):
        sys.exit("error: cov[9] did not round-trip")
    print(f"valid_packet     {len(valid)} B  crc=0x{vals['crc32']:08X}  parsed OK")

    # ── bad_crc_packet — same packet, CRC MSB flipped; must be REJECTED ─────
    bad = bytearray(valid)
    bad[-1] ^= 0xFF
    bad = bytes(bad)
    if ref._parse(bad) is not None:
        sys.exit("error: reference parser ACCEPTED the corrupt packet")
    print(f"bad_crc_packet   {len(bad)} B  rejected OK")

    # ── resync_stream — garbage w/ decoy magic, then two valid frames ───────
    fa, va = build({"imu_seq": 1000, "heading_deg": 90.5})
    fb, vb = build({"imu_seq": 1001, "heading_deg": 91.5,
                    "ts_wall_ns": vals["ts_wall_ns"] + 10_000_000})
    stream = GARBAGE + fa + fb
    for frame, want_seq in ((fa, 1000), (fb, 1001)):
        q = ref._parse(frame)
        if q is None or q.imu_seq != want_seq:
            sys.exit(f"error: resync frame seq {want_seq} did not round-trip")
    print(f"resync_stream    {len(stream)} B  "
          f"({len(GARBAGE)} garbage + 2 x {PACKET_SIZE})  "
          f"crcA=0x{va['crc32']:08X} crcB=0x{vb['crc32']:08X}")

    # ── write ───────────────────────────────────────────────────────────────
    (outdir / "valid_packet.hex").write_text(to_hex(valid))
    (outdir / "bad_crc_packet.hex").write_text(to_hex(bad))
    (outdir / "resync_stream.hex").write_text(to_hex(stream))

    (outdir / "c_arrays.txt").write_text(
        "/* Generated by gen_vectors.py — paste into\n"
        " * imud-arduino/test/test_parser/test_parser.cpp, replacing the\n"
        " * existing kValidPacket / kBadCrcPacket / kResyncStream arrays.\n"
        " * These are byte-identical to the .hex files beside this one. */\n\n"
        + to_c_array("kValidPacket", valid) + "\n\n"
        + to_c_array("kBadCrcPacket", bad) + "\n\n"
        + to_c_array("kResyncStream", stream) + "\n")

    write_md(outdir / "valid_packet.md", vals, va, vb, len(stream))
    print(f"\nwrote 5 files to {outdir}/ — all self-verified")


def write_md(path, vals, va, vb, stream_len):
    lines = [
        "# Golden test vectors — imud wire packet v17 (276 bytes, little-endian)",
        "",
        "Hex files: lowercase hex, 32 bytes (64 chars) per line, LF line",
        "endings. Strip whitespace and hex-decode to get the raw bytes.",
        "All vectors were generated by, and verified against, imud's reference",
        "Python client (`lib/imud_client.py` `_parse()`) — see",
        "`tools/gen_vectors.py`.",
        "",
        "**Changed from v14:** four float32 fields are appended after",
        "`mag_residual` (`innov_weight`, `innov_reject`, `nis_accel`,",
        "`nis_mag`), so the packet grows 260 → 276 bytes and `crc32` moves",
        "from offset 256 to **272**, now covering bytes 0–271. Every other",
        "field keeps its offset and value.",
        "",
        "## valid_packet.hex — one valid packet",
        "",
        "Exact decoded field values (all floats are exact binary fractions —",
        "compare with `==` against these decimals after float32 decode):",
        "",
        "```",
    ]
    for name, code, _ in FIELDS:
        v = vals[name]
        if name == "magic":
            lines.append(f"{name:<18} = {v}  (0x{v:08X})")
        elif name == "flags":
            lines.append(f"{name:<18} = {v}  (0x{v:04X})")
        elif code == "f":
            lines.append(f"{name:<18} = {v!r}")
        else:
            lines.append(f"{name:<18} = {v}")
    c = vals["crc32"]
    lines += [
        f"{'crc32':<18} = {c}  (0x{c:08X}, stored little-endian at offset 272;",
        f"{'':<20} IEEE 802.3 CRC32 of bytes 0..271)",
        "```",
        "",
        "Derived expectations:",
        "- true heading = heading_deg + declination_deg = "
        f"{vals['heading_deg'] + vals['declination_deg']}",
        "- flags set: MAG_VALID, FUSION_CONVERGED, ACCEL_CAL, GYRO_CAL,",
        f"  MAG_CAL, DECLINATION_VALID, HEAVE_VALID, WAVE_VALID (= 0x{vals['flags']:04X})",
        "- the quaternion is NOT unit norm — these are parser test values,",
        "  not physics; nothing in the protocol validates the norm",
        "- `innov_weight` = 1.0 and `innov_reject` = 0.0 is the healthy case:",
        "  the Huber cap never fired and no update was gate-rejected. The NIS",
        "  pair straddles 1.0, which is the consistent value.",
        "",
        "## bad_crc_packet.hex — must be REJECTED",
        "",
        "Same packet with the last byte (CRC MSB) XOR 0xFF. A correct parser",
        "returns no packet (validation order: size, magic, version, CRC).",
        "",
        "## resync_stream.hex — mid-stream resync",
        "",
        f"Layout ({stream_len} bytes total):",
        f"- bytes 0..{len(GARBAGE) - 1}: {len(GARBAGE)} bytes of garbage. A DECOY magic",
        "  sequence (44 55 4d 49) sits at offset 7, followed by a plausible",
        "  version (11 00 = 17) — a parser will false-lock there and must",
        "  recover via CRC failure + rescan (drop one byte, scan for the next",
        "  magic; never discard the whole buffer).",
        f"- bytes {len(GARBAGE)}..{len(GARBAGE) + PACKET_SIZE - 1}: valid frame A "
        f"(imu_seq={va['imu_seq']}, heading_deg={va['heading_deg']},",
        f"  crc32=0x{va['crc32']:08X})",
        f"- bytes {len(GARBAGE) + PACKET_SIZE}..{stream_len - 1}: valid frame B "
        f"(imu_seq={vb['imu_seq']}, heading_deg={vb['heading_deg']},",
        f"  ts_wall_ns=+10ms, crc32=0x{vb['crc32']:08X})",
        "",
        "Expected outcome, whether fed all at once or one byte at a time:",
        f"exactly 2 valid packets, seq {va['imu_seq']} then {vb['imu_seq']}, and no packet",
        "emitted from the decoy lock.",
        "",
    ]
    path.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
