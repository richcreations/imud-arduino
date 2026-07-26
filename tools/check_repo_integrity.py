#!/usr/bin/env python3
#
# imud-arduino — repository consistency checks for CI
# Copyright (c) 2026 Richard Simpson
# SPDX-License-Identifier: MIT
#
"""Catch the ways this repo can quietly drift out of sync with itself.

Every check here corresponds to a mistake that is easy to make, produces a
GREEN build, and is only noticed much later:

  1. An example sketch exists but no CI job compiles it. Examples are
     enumerated by name in ci.yml, not globbed, so a new one is silently
     never built. (This happened when HelloAttitude was added.)
  2. library.properties, library.json and CHANGELOG.md disagree about the
     version. Nothing builds these against each other.
  3. The byte arrays in test/test_parser/test_parser.cpp drift from
     extras/golden/*.hex. The tests keep passing against the arrays while
     the .hex files — which the docs point at as the reference — say
     something different.
  4. A public API symbol is missing from keywords.txt, so the Arduino IDE
     silently stops highlighting it.

Run with no arguments from the repository root:

    python3 tools/check_repo_integrity.py
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
failures = []
notes = []


def fail(check, msg):
    failures.append(f"[{check}] {msg}")


def ok(check, msg):
    notes.append(f"  ok   {check}: {msg}")


# ── 1. every example sketch is compiled by CI ────────────────────────────────

def check_examples_covered():
    sketches = sorted(p for p in (ROOT / "examples").glob("*/*.ino"))
    if not sketches:
        fail("examples", "no example sketches found at all")
        return

    ci = (ROOT / ".github/workflows/ci.yml").read_text()
    missing = []
    for s in sketches:
        rel = s.relative_to(ROOT).as_posix()
        if rel not in ci:
            missing.append(rel)
    if missing:
        fail("examples",
             "example(s) present but never compiled by .github/workflows/ci.yml: "
             + ", ".join(missing)
             + "\n         Examples are listed by name there, not globbed — add a"
               " step for each.")
    else:
        ok("examples", f"all {len(sketches)} sketches are compiled by CI")


# ── 2. version agreement ─────────────────────────────────────────────────────

def check_versions():
    props = (ROOT / "library.properties").read_text()
    m = re.search(r"^version=(.+)$", props, re.M)
    if not m:
        fail("version", "library.properties has no version= line")
        return
    prop_v = m.group(1).strip()

    json_v = json.loads((ROOT / "library.json").read_text())["version"].strip()

    changelog = (ROOT / "CHANGELOG.md").read_text()
    cm = re.search(r"^## \[([0-9]+\.[0-9]+\.[0-9]+)\]", changelog, re.M)
    if not cm:
        fail("version", "CHANGELOG.md has no '## [x.y.z]' section")
        return
    chg_v = cm.group(1)

    if not (prop_v == json_v == chg_v):
        fail("version",
             f"version mismatch — library.properties={prop_v}, "
             f"library.json={json_v}, newest CHANGELOG entry={chg_v}")
    else:
        ok("version", f"library.properties, library.json and CHANGELOG agree ({prop_v})")


# ── 3. test arrays match the golden hex vectors byte for byte ────────────────

def check_golden_vectors():
    pairs = [
        ("kValidPacket", "valid_packet.hex"),
        ("kBadCrcPacket", "bad_crc_packet.hex"),
        ("kResyncStream", "resync_stream.hex"),
    ]
    cpp = (ROOT / "test/test_parser/test_parser.cpp").read_text()

    for name, hexfile in pairs:
        hp = ROOT / "extras/golden" / hexfile
        if not hp.exists():
            fail("golden", f"missing {hp.relative_to(ROOT)}")
            continue
        want = bytes.fromhex("".join(hp.read_text().split()))

        m = re.search(rf"{name}\[(\d+)\]\s*=\s*\{{(.*?)\}};", cpp, re.S)
        if not m:
            fail("golden", f"array {name} not found in test_parser.cpp")
            continue
        declared = int(m.group(1))
        got = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2)))

        if declared != len(got):
            fail("golden",
                 f"{name} declares [{declared}] but contains {len(got)} bytes")
        elif got != want:
            fail("golden",
                 f"{name} in test_parser.cpp does not match "
                 f"extras/golden/{hexfile} ({len(got)} vs {len(want)} bytes)."
                 "\n         Regenerate both with tools/gen_vectors.py rather"
                 " than editing either by hand.")
        else:
            ok("golden", f"{name} matches {hexfile} ({len(got)} bytes)")


# ── 4. public API symbols appear in keywords.txt ─────────────────────────────

def check_keywords():
    header = (ROOT / "src/ImudClient.h").read_text()
    keywords = (ROOT / "keywords.txt").read_text()

    symbols = set(re.findall(r"^#define\s+(IMUD_[A-Z0-9_]+)", header, re.M))
    symbols |= set(re.findall(r"^inline\s+\w+\s+(imud_\w+)\s*\(", header, re.M))
    # Internal-only hooks are deliberately not user-facing.
    symbols -= {"IMUD_ASSERT", "IMUD_PACKED_ATTR", "IMUD_CLIENT_H"}

    missing = sorted(s for s in symbols
                     if not re.search(rf"^{re.escape(s)}\s", keywords, re.M))
    if missing:
        fail("keywords",
             "public symbol(s) missing from keywords.txt (Arduino IDE "
             "highlighting): " + ", ".join(missing))
    else:
        ok("keywords", f"all {len(symbols)} public symbols are listed")


# ── 5. every relative link and heading anchor resolves ───────────────────────

def _slug(heading):
    s = heading.strip().lower()
    s = re.sub(r"[^\w\s-]", "", s)
    return re.sub(r"[-\s]+", "-", s).strip("-")


def check_links():
    docs = sorted(set(list(ROOT.glob("*.md")) + list(ROOT.glob("docs/*.md"))))
    anchors = {}
    for d in docs:
        anchors[d] = {_slug(m.group(2))
                      for m in re.finditer(r"^(#{1,6})\s+(.*)$", d.read_text(), re.M)}

    broken = 0
    for d in docs:
        for m in re.finditer(r"\[([^\]]*)\]\(([^)\s]+)\)", d.read_text()):
            target = m.group(2)
            if target.startswith(("http://", "https://", "mailto:", "#!")):
                continue
            path_part, _, anchor = target.partition("#")
            dest = d
            if path_part:
                dest = (d.parent / path_part).resolve()
                if not dest.exists():
                    fail("links", f"{d.relative_to(ROOT)}: broken path -> {target}")
                    broken += 1
                    continue
            if anchor and dest in anchors and anchor not in anchors[dest]:
                fail("links",
                     f"{d.relative_to(ROOT)}: anchor not found -> {target}")
                broken += 1
    if not broken:
        ok("links", f"all relative links and anchors resolve across {len(docs)} docs")


def main():
    for fn in (check_examples_covered, check_versions,
               check_golden_vectors, check_keywords, check_links):
        try:
            fn()
        except Exception as e:                      # noqa: BLE001
            fail(fn.__name__, f"check raised {type(e).__name__}: {e}")

    print("\n".join(notes))
    if failures:
        print("\nrepository integrity FAILED:\n")
        for f in failures:
            print(f"  {f}")
        return 1
    print("\nrepository integrity OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
