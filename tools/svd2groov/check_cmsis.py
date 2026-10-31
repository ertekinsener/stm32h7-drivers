#!/usr/bin/env python3
"""Compares the generated groov register maps with the CMSIS headers.

ST's CMSIS device header (stm32h755xx.h) and ARM's core_cm7.h are written
independently of the SVD files, so they catch SVD errors that the config
does not correct yet:

  check_cmsis.py --cmsis <cmsis-device-h7>/Include/stm32h755xx.h \\
                 --cmsis <CMSIS_6>/CMSIS/Core/Include/core_cm7.h

For every field of the groov maps (include/stm32h7/regs/*.h) whose CMSIS mask
macro is found by name (<PERIPH>_<REG>_<FIELD>_Msk, with the CMSIS names of
register arrays: DMA_SxCR, DMAMUX_CxCR; bank 2 flash registers as
FLASH_<REG>_<FIELD>), the masks must be equal; every number of stm32h7::Irq
must be a CMSIS IRQn and the other way round. Fields without a CMSIS macro
of the same name are listed. Exit status 1 on a mismatch.
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _flash(register, field):
    return register.removesuffix("2"), field.removesuffix("2")


# Map header -> (CMSIS prefix, register name -> CMSIS register name).
MAPS = {
    "rcc": ("RCC", lambda r, f: (r, f)),
    "rng": ("RNG", lambda r, f: (r, f)),
    "gpio": ("GPIO", lambda r, f: (r, f)),
    "flash": ("FLASH", _flash),
    "usart": ("USART", lambda r, f: (r, f)),
    "dma": ("DMA", lambda r, f: (re.sub(r"^S[0-7]", "Sx", r), f)),
    "dmamux1": ("DMAMUX", lambda r, f: (re.sub(r"^C[0-9]+", "Cx", r), f)),
    "scb": ("SCB", lambda r, f: (r, f)),
}


# CMSIS macros that differ from the H755 by design, with the reason.
KNOWN = {
    "DMAMUX_CxCR_DMAREQ_ID": "the CMSIS macro is the widest of the family: 8 "
                             "bits for request ids up to 137 (H72x/H73x); "
                             "the H755 DMAMUX1 ids end at 115 (ADC3)",
    "DMAMUX_CxCR_SYNC_ID": "the CMSIS macro is the widest of the family (5 "
                           "bits); 3 bits in the H755 SVD",
}


def cmsis_masks(text):
    positions = {name: int(value, 0) for name, value in
                 re.findall(r"#define\s+(\w+)_Pos\s+\(?(\d+)U?L?\)?", text)}
    masks = {}
    for name, value, pos in re.findall(
            r"#define\s+(\w+)_Msk\s+\((0x[0-9A-Fa-f]+|\d+)UL?\s*<<\s*(\w+)_Pos\)",
            text):
        if pos in positions:
            masks[name] = int(value, 0) << positions[pos]
    # Aliases: #define GPIO_MODER_MODE0_Msk GPIO_MODER_MODER0_Msk.
    for name, target in re.findall(r"#define\s+(\w+)_Msk\s+(\w+)_Msk\b", text):
        if target in masks:
            masks[name] = masks[target]
    return masks


def map_fields(path):
    """[(register, field, mask)] of a generated groov map."""
    fields, register = [], None
    for line in path.read_text().splitlines():
        if m := re.search(r'groov::reg<"(\w+)"', line):
            register = m.group(1).upper()
        elif m := re.search(r'groov::field<"(\w+)", [\w:]+, (\d+)u, (\d+)u', line):
            msb, lsb = int(m.group(2)), int(m.group(3))
            fields.append((register, m.group(1).upper(),
                           ((1 << (msb - lsb + 1)) - 1) << lsb))
    return fields


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cmsis", type=pathlib.Path, action="append", required=True,
                        help="CMSIS header; give the device and the core header")
    args = parser.parse_args()
    text = "\n".join(path.read_text() for path in args.cmsis)
    masks = cmsis_masks(text)
    irqs = {int(v): n for n, v in re.findall(r"\b(\w+)_IRQn\s*=\s*(\d+)", text)}

    errors, known, unmatched, matched, seen = [], [], [], 0, set()
    for name, (prefix, rename) in MAPS.items():
        for register, field, mask in map_fields(ROOT / f"include/stm32h7/regs/{name}.h"):
            cmsis_register, cmsis_field = rename(register, field)
            macro = f"{prefix}_{cmsis_register}_{cmsis_field}"
            if (macro, mask) in seen:
                continue  # Another instance or array element.
            seen.add((macro, mask))
            if macro not in masks:
                unmatched.append(f"{name}: {register}.{field} ({macro})")
            elif masks[macro] != mask:
                line = (f"{name}: {register}.{field} = {mask:#010x}, "
                        f"{macro}_Msk = {masks[macro]:#010x}")
                if macro in KNOWN:
                    known.append(f"{line} ({KNOWN[macro]})")
                else:
                    errors.append(line)
            else:
                matched += 1

    ours = {int(n): c for c, n in re.findall(
        r"^\s+(k\w+) = (\d+),", (ROOT / "include/stm32h7/irq.h").read_text(), re.M)}
    for number, constant in sorted(ours.items()):
        if number not in irqs:
            errors.append(f"irq.h: {constant} = {number} is no CMSIS IRQn")
    for number, name in sorted(irqs.items()):
        if number not in ours:
            errors.append(f"irq.h: {name}_IRQn = {number} missing")

    print(f"{matched} fields and {len(ours)} interrupts match")
    for line in unmatched:
        print(f"no CMSIS macro of the same name: {line}")
    for line in known:
        print(f"known difference: {line}")
    for line in errors:
        print(f"MISMATCH {line}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
