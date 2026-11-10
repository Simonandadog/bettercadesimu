#!/usr/bin/env python3
"""Build data/palette.json and docs/palette.md from the reference section files.

The reference `.cad` files and screenshots live in "everything files/" which is
not committed (they are the user's survey sheets). This script is committed so
the catalogue can be regenerated when those files are present.

Names for sections 1-6 come from "everything files/everything.md". Items are
listed in *palette order*, which is the order they were placed on the survey
sheet (record order), not ascending type. Accidental duplicate placements and
child/overlay records are skipped.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / "everything files"
OUT_JSON = ROOT / "data" / "palette.json"
OUT_MD = ROOT / "docs" / "palette.md"

# type -> human name, for the sections the user already labelled.
NAMES: dict[int, str] = {
    # section 1 - Power feedings
    3000: "Phase power",
    3001: "Power Neutro",
    3002: "Power Protection",
    3003: "Power F+N",
    3011: "Power F+N+PE",
    3004: "Power L1+L2+L3",
    3005: "Power L1+L2+L3+PE",
    3006: "Power L1+L2+L3+N",
    3007: "Power L1+L2+L3+N+PE",
    3008: "Positive power",
    3009: "Negative power",
    3010: "Power +-",
    3015: "Power Protection",
    3012: "Transformer",
    3017: "Transformer III",
    3016: "Diode",
    3013: "Rectifier",
    3014: "Power supply",
    3018: "Power supply III",
    3019: "Battery",
    3020: "Eletrical outlet",
    # section 2 - Fuses, Disconnects
    5000: "Fuse I",
    5001: "Fuse IN",
    5002: "Fuse II",
    5003: "Fuse III",
    5004: "Fuse IIIN",
    5005: "Fuse disconnector I",
    5006: "Fuse disconnector IN",
    5007: "Fuse disconnector II",
    5008: "Fuse disconnector III",
    5009: "Fuse disconnector IIIN",
    # section 3 - Automatic, Magnetothermic
    6000: "Automatic switch I",
    6001: "Automatic switch IN",
    6002: "Automatic switch II",
    6003: "Automatic switch III",
    6004: "Automatic switch IIIN",
    6005: "Differential switch IN",
    6006: "Differential switch IIIN",
    6010: "Magnetothermic switch I",
    6011: "Magnetothermic switch IN",
    6008: "Magnetothermic switch II",
    6009: "Magnetothermic switch III",
    6012: "Magnetothermic switch IIIN",
    6013: "Overvoltage limiter IN",
    6014: "Overvoltage limiter IIIN",
    # 6007 is an unlabelled extra that appears between the two groups.
    # section 4 - Contactors Switches
    2006: "Contactor I",
    2000: "Contactor II",
    2001: "Contactor III",
    2002: "Contactor IIII",
    2008: "Contactor III (alternate)",
    2007: "Switch I",
    2003: "Switch II",
    2004: "Switch III",
    2005: "Switch IIII",
    # section 5 - Motors
    1000: "Three-phase motor",
    1001: "Three-phase motor YD",
    1002: "Single-phase motor",
    1008: "Single-phase motor (capacitor)",
    1010: "",  # palette entry without a tooltip
    1003: "Motor Dahlander",
    1005: "Two speed motor",
    1004: "Wound rotor induction motor",
    1006: "DC motor, permanent magnet",
    1007: "DC motor, independent field",
    1009: "Brake",
    10550: "Electric linear actuator",
    # section 6 - Power
    1050: "Resistance I",
    1051: "Resistance II",
    1052: "Resistance III",
    1053: "Electronic starter a.c.",
    1062: "Electronic starter control a.c.",
    1054: "Variable speed a.c. II",
    1055: "Variable speed a.c. III",
    1056: "Drive speed control a.c.",
    1057: "Variable speed d.c. II",
    1058: "Variable speed d.c. II (alternate)",
    1059: "Variable speed d.c. III",
    1060: "Variable speed d.c. III (alternate)",
    1061: "Drive speed control d.c.",
}

SECTION_TITLES: dict[int, str] = {
    1: "Power feedings",
    2: "Fuses, Disconnects",
    3: "Automatic, Magnetothermic",
    4: "Contactors Switches",
    5: "Motors",
    6: "Power",
}

# Records that are not palette entries.
SKIP: dict[int, set[tuple[int, int]]] = {
    1: {(126, 0)},      # stray second copy of type 3010
    5: {(198, 15)},     # stray second copy of type 1010
}

# Types that leaked into a survey sheet from another palette section.
SKIP_TYPES: dict[int, set[int]] = {
    12: {4018},
    13: {3008, 3009, 4018, 4019},
}


def section_number(path: Path) -> int:
    match = re.match(r"section\s*(\d+)", path.name, re.IGNORECASE)
    if not match:
        raise ValueError(f"unexpected file name: {path.name}")
    return int(match.group(1))


def parse(path: Path):
    text = path.read_bytes().decode("latin1")
    body = text.split("$$$")[0]
    tokens = body.split("*")
    if not tokens or tokens[0] != "CADe_SIMU":
        raise ValueError(f"{path.name}: missing CADe_SIMU header")

    i = 1
    while i < len(tokens):
        if tokens[i] == "" and i == len(tokens) - 1:
            break
        oid = int(tokens[i])
        parts = tokens[i + 1].split("#")
        type_code = int(parts[0])
        pin_flags = max(8, len(parts) - 3)
        need = 2 + pin_flags + 15
        fields = [int(x.split("#")[0] or 0) for x in tokens[i + 2 : i + need]]
        tail = fields[pin_flags:]
        yield {
            "id": oid,
            "type": type_code,
            "label": parts[1] if len(parts) > 1 else "",
            "pins": [p for p in parts[3:] if p],
            "origin": [tail[0], tail[1]],
            "box": [tail[6], tail[7]],
        }
        i += need


def build():
    files = sorted(
        (
            p
            for p in REF.iterdir()
            if p.is_file() and p.suffix.lower() != ".png" and p.name != "everything.md"
        ),
        key=section_number,
    )

    sections = []
    for path in files:
        number = section_number(path)
        items = []
        seen_types: set[int] = set()
        for record in parse(path):
            if record["type"] < 0:
                continue  # child/overlay record, not a palette entry
            if record["type"] in SKIP_TYPES.get(number, set()):
                continue
            if tuple(record["origin"]) in SKIP.get(number, set()):
                continue
            if record["type"] in seen_types:
                continue  # keep the first placement of a repeated type
            seen_types.add(record["type"])
            items.append(
                {
                    "type": record["type"],
                    "name": NAMES.get(record["type"]),
                    "label": record["label"],
                    "pins": record["pins"],
                    "origin": record["origin"],
                    "box": record["box"],
                }
            )

        sections.append(
            {
                "number": number,
                "title": SECTION_TITLES.get(number, f"Section {number}"),
                "source": path.name,
                "items": items,
            }
        )

    return {"version": 1, "unitScale": 4.0, "sections": sections}


def to_markdown(catalogue) -> str:
    lines = [
        "# Symbol palette catalogue",
        "",
        "Generated by `tools/build_palette.py` from the reference section sheets.",
        "Items are in palette order. Unnamed entries still need a label.",
        "",
        "| Section | Type | Name | Pins | Box |",
        "|---:|---:|---|---|---|",
    ]
    for section in catalogue["sections"]:
        for item in section["items"]:
            name = item["name"] or "—"
            pins = ", ".join(item["pins"]) or "—"
            box = f'{item["box"][0]}×{item["box"][1]}'
            lines.append(
                f'| {section["number"]} | {item["type"]} | {name} | {pins} | {box} |'
            )
    lines.append("")
    total = sum(len(s["items"]) for s in catalogue["sections"])
    named = sum(1 for s in catalogue["sections"] for i in s["items"] if i["name"])
    lines += [f"{total} items, {named} named.", ""]
    return "\n".join(lines)


def main():
    catalogue = build()
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(catalogue, indent=2, ensure_ascii=False) + "\n")

    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text(to_markdown(catalogue))

    total = sum(len(s["items"]) for s in catalogue["sections"])
    named = sum(1 for s in catalogue["sections"] for i in s["items"] if i["name"])
    print(f"wrote {OUT_JSON.relative_to(ROOT)} and {OUT_MD.relative_to(ROOT)}")
    print(f"{len(catalogue['sections'])} sections, {total} items, {named} named")


if __name__ == "__main__":
    main()
