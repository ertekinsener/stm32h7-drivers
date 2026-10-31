#!/usr/bin/env python3
"""Generates the groov register maps of stm32h7-drivers from CMSIS-SVD files.

For every [[map]] of the TOML config (one peripheral, one or more instances)
it writes include/stm32h7/regs/<name>.h: the groov::group of the
peripheral's registers, templated on the base address, and one constant per
instance. include/stm32h7/irq.h lists the interrupt numbers.

The config selects the registers and corrects the SVD where it is wrong or
where groov needs more than the SVD says (write functions of write-1
registers, read-only fields in read-write registers, ...). Only the Python
standard library is used.

  svd2groov.py --config stm32h7.toml           # (re)write the headers
  svd2groov.py --config stm32h7.toml --check   # exit 1 if any is stale

The device SVD is downloaded from the pinned URL in the config (checked
against its SHA-256) into a cache directory, or given with --svd.
"""

import argparse
import copy
import dataclasses
import fnmatch
import hashlib
import os
import pathlib
import re
import sys
import textwrap
import tomllib
import urllib.request
import xml.etree.ElementTree as ET

# Access kinds of the config and the groov write function of each.
ACCESS = {
    "read_write": "groov::w::replace",
    "read_only": "groov::read_only<groov::w::ignore>",
    "write_only": "groov::write_only<groov::w::replace>",
    "write_one_to_set": "groov::write_only<groov::w::one_to_set>",
    "write_one_to_clear": "groov::write_only<groov::w::one_to_clear>",
    "read_one_to_clear": "groov::w::one_to_clear",
    "read_zero_to_clear": "groov::w::zero_to_clear",
}

# SVD <access> values.
SVD_ACCESS = {
    "read-write": "read_write",
    "read-writeOnce": "read_write",
    "read-only": "read_only",
    "write-only": "write_only",
    "writeOnce": "write_only",
}

REGISTER_TYPES = {8: "std::uint8_t", 16: "std::uint16_t", 32: "std::uint32_t"}


class ConfigError(Exception):
    pass


@dataclasses.dataclass
class Field:
    name: str
    msb: int
    lsb: int
    access: str


@dataclasses.dataclass
class Register:
    name: str
    offset: int
    size: int
    access: str
    fields: list[Field]


@dataclasses.dataclass
class Peripheral:
    name: str
    base: int
    registers: list[Register]


def _text(element, tag, default=None):
    child = element.find(tag)
    if child is None or child.text is None:
        return default
    return child.text.strip()


def _int(text):
    return int(text, 0)


def _svd_access(element, inherited):
    access = _text(element, "access")
    kind = SVD_ACCESS[access] if access else inherited
    modified = _text(element, "modifiedWriteValues")
    if modified == "oneToClear":
        kind = "read_one_to_clear" if kind != "write_only" else "write_one_to_clear"
    elif modified == "zeroToClear" and kind != "write_only":
        kind = "read_zero_to_clear"
    elif modified == "oneToSet" and kind == "write_only":
        kind = "write_one_to_set"
    elif modified not in (None, "modify"):
        raise ConfigError(f"modifiedWriteValues {modified} is not supported")
    return kind


def _bit_range(field):
    if (bit_range := _text(field, "bitRange")) is not None:
        msb, lsb = re.fullmatch(r"\[(\d+):(\d+)\]", bit_range).groups()
        return int(msb), int(lsb)
    if (lsb := _text(field, "lsb")) is not None:
        return int(_text(field, "msb")), int(lsb)
    offset = int(_text(field, "bitOffset"))
    return offset + int(_text(field, "bitWidth", "1")) - 1, offset


class Device:
    """An SVD file; peripherals are parsed when asked for."""

    def __init__(self, path):
        self._root = ET.parse(path).getroot()
        self._defaults = {
            "access": SVD_ACCESS.get(_text(self._root, "access"), "read_write"),
            "size": _text(self._root, "size", "32"),
        }
        self.elements = {_text(p, "name"): p
                         for p in self._root.iterfind("peripherals/peripheral")}
        self.nvic_priority_bits = int(_text(self._root, "cpu/nvicPrioBits", "4"))

    def peripheral(self, name):
        element = self.elements[name]
        source = element
        if (parent := element.get("derivedFrom")) is not None:
            source = self.elements[parent]
        defaults = {
            "access": SVD_ACCESS.get(_text(source, "access"),
                                     self._defaults["access"]),
            "size": _text(source, "size", self._defaults["size"]),
        }
        if source.find("registers/cluster") is not None:
            raise ConfigError(f"{name}: SVD <cluster> is not supported")
        registers = []
        for element_register in source.iterfind("registers/register"):
            registers += self._registers(element_register, name, defaults)
        return Peripheral(name, _int(_text(element, "baseAddress")), registers)

    def interrupts(self):
        """{number: name} of every <interrupt> of every peripheral."""
        interrupts = {}
        for element in self.elements.values():
            for interrupt in element.iterfind("interrupt"):
                number = int(_text(interrupt, "value"))
                name = _text(interrupt, "name")
                if interrupts.setdefault(number, name) != name:
                    raise ConfigError(f"interrupt {number} is both "
                                      f"{interrupts[number]} and {name}")
        return interrupts

    @staticmethod
    def _registers(element, peripheral, defaults):
        """The register, or the <dim> expansion of a register array."""
        name = _text(element, "name")
        access = _svd_access(element, defaults["access"])
        fields = []
        for field in element.iterfind("fields/field"):
            msb, lsb = _bit_range(field)
            fields.append(Field(_text(field, "name"), msb, lsb,
                                _svd_access(field, access)))
        offset = _int(_text(element, "addressOffset"))
        size = _int(_text(element, "size", defaults["size"]))
        if (dim := _text(element, "dim")) is None:
            return [Register(name, offset, size, access, fields)]
        indices = (_text(element, "dimIndex") or "").split(",")
        if indices == [""]:
            indices = [str(i) for i in range(_int(dim))]
        increment = _int(_text(element, "dimIncrement"))
        if "%s" not in name or len(indices) != _int(dim):
            raise ConfigError(f"{peripheral}.{name}: unsupported <dim>")
        return [Register(name.replace("%s", index), offset + i * increment, size,
                         access, copy.deepcopy(fields))
                for i, index in enumerate(indices)]


class Devices:
    """The device SVD and the supplementary SVDs (the core)."""

    def __init__(self, devices):
        self.devices = devices

    def peripheral(self, name):
        for device in self.devices:
            if name in device.elements:
                return device.peripheral(name)
        raise ConfigError(f"no peripheral {name} in the SVD files")


# --- Corrections -------------------------------------------------------------


def _matches(patterns, name, used):
    """Returns the value of the first pattern matching name (case-insensitive)."""
    for pattern, value in patterns.items():
        if fnmatch.fnmatchcase(name.upper(), pattern.upper()):
            used.add(pattern)
            return value
    return None


def _ordered_insert(fields, new):
    """Inserts new keeping the bit order (ascending or descending) of fields."""
    descending = len(fields) > 1 and fields[0].lsb > fields[-1].lsb
    for i, field in enumerate(fields):
        if (field.lsb < new.lsb) if descending else (field.lsb > new.lsb):
            fields.insert(i, new)
            return
    fields.append(new)


def _fixes(layer, fix_sets):
    """Merges the named fix sets and the layer's own tables."""
    merged = {"access": {}, "rename": {}, "add": {}, "redefine": {}}
    for name in layer.get("fixes", []):
        if name not in fix_sets:
            raise ConfigError(f"no fix set '{name}'")
        for table in merged:
            merged[table].update(fix_sets[name].get(table, {}))
    for table in merged:
        merged[table].update(layer.get(table, {}))
    return merged


def _spec_field(name, spec, register_access):
    msb = spec.get("msb", spec.get("bit"))
    lsb = spec.get("lsb", spec.get("bit"))
    if msb is None or lsb is None or msb < lsb:
        raise ConfigError(f"field {name}: give bit, or msb and lsb")
    return Field(name, msb, lsb, spec.get("access", register_access))


def apply_config(peripheral, layer, fix_sets):
    """Selects and corrects the registers of one instance."""
    selection = layer.get("registers")
    fixes = _fixes(layer, fix_sets)
    access, rename, add, redefine = (fixes[t] for t in
                                     ("access", "rename", "add", "redefine"))
    kinds = [*access.values(),
             *(a.get("access", "read_write") for a in add.values()),
             *(f.get("access", "read_write") for fs in redefine.values() for f in fs)]
    for kind in kinds:
        if kind not in ACCESS:
            raise ConfigError(f"unknown access '{kind}', one of {', '.join(ACCESS)}")
    used = set()
    svd_names = [r.name for r in peripheral.registers]

    registers = []
    for register in peripheral.registers:
        if selection is not None and not any(
                fnmatch.fnmatchcase(register.name.upper(), p.upper())
                for p in selection):
            continue
        if (fields := _matches(redefine, register.name, used)) is not None:
            register.fields = [_spec_field(f["name"], f, register.access)
                               for f in fields]
        for path, spec in add.items():
            register_pattern, _, name = path.rpartition(".")
            if not fnmatch.fnmatchcase(register.name.upper(),
                                       register_pattern.upper()):
                continue
            used.add(path)
            if any(f.name.upper() == name.upper() for f in register.fields):
                raise ConfigError(f"{register.name}.{name} is in the SVD already")
            _ordered_insert(register.fields,
                            _spec_field(name, spec, register.access))
        if (kind := _matches(access, register.name, used)) is not None:
            register.access = kind
            for field in register.fields:
                field.access = kind
        for field in register.fields:
            path = f"{register.name}.{field.name}"
            if (kind := _matches(access, path, used)) is not None:
                field.access = kind
            if (name := _matches(rename, path, used)) is not None:
                field.name = name
        if (name := _matches(rename, register.name, used)) is not None:
            register.name = name
        if prefix := layer.get("strip_prefix"):
            register.name = register.name.removeprefix(prefix)
        registers.append(register)

    for pattern in [*access, *rename, *add, *redefine]:
        if pattern not in used:
            raise ConfigError(f"'{pattern}' matches nothing in {peripheral.name}")
    for pattern in selection or []:
        if not any(fnmatch.fnmatchcase(name.upper(), pattern.upper())
                   for name in svd_names):
            raise ConfigError(f"register '{pattern}' matches nothing in "
                              f"{peripheral.name}")
    if not registers:
        raise ConfigError("no registers selected")
    return registers


# --- Rendering ---------------------------------------------------------------


def _camel(name):
    return "".join(part[:1].upper() + part[1:].lower()
                   for part in re.split(r"[^A-Za-z0-9]+", name) if part)


def _constant(name):
    """SVD name -> Google-style constant name: CLR_EOP2 -> kClrEop2."""
    return "k" + _camel(name)


def _width(field):
    return field.msb - field.lsb + 1


def _field_type(width):
    if width == 1:
        return "bool"
    for bits in (8, 16, 32):
        if width <= bits:
            return f"std::uint{bits}_t"
    raise ConfigError(f"field of {width} bits")


def _comment(text):
    """Wraps text into // lines; blank lines separate paragraphs."""
    lines = []
    for paragraph in re.split(r"\n\s*\n", text.strip()):
        if lines:
            lines.append("//")
        lines += textwrap.wrap(" ".join(paragraph.split()), width=77,
                               initial_indent="// ", subsequent_indent="// ",
                               break_on_hyphens=False, break_long_words=False)
    return "".join(f"{line}\n" for line in lines)


def _guard(prefix, path):
    return prefix + re.sub(r"[^A-Z0-9]", "_", path.upper()).removesuffix("_H") + "_"


def _wrap_header(path, prefix, includes, comment, namespace, body):
    guard = _guard(prefix, path)
    return (f"#ifndef {guard}\n#define {guard}\n\n{includes}\n{comment}"
            f"namespace {namespace} {{\n\n{body}\n\n}}  // namespace {namespace}\n\n"
            f"#endif  // {guard}\n")


def _layout(registers):
    return [(r.name, r.offset, r.size, r.access,
             [(f.name, f.msb, f.lsb, f.access) for f in r.fields]) for r in registers]


class GroovMap:
    """One [[map]]: the groov::group of a peripheral and its instances."""

    def __init__(self, table, config, devices):
        self.config = table
        self.name = table["name"]
        self.namespace = f"stm32h7::regs::{self.name}"
        self.instances = []  # (instance name, base)
        self.registers = None
        for instance in table["instances"]:
            peripheral = devices.peripheral(instance)
            registers = apply_config(peripheral, table, config.get("fixes", {}))
            if self.registers is None:
                self.registers = registers
            elif _layout(registers) != _layout(self.registers):
                raise ConfigError(f"{instance} differs from {self.instances[0][0]}")
            self.instances.append((instance, peripheral.base))
        for register in self.registers:
            position = 0
            for field in sorted(register.fields, key=lambda f: f.lsb):
                if field.lsb < position:
                    raise ConfigError(f"{register.name}.{field.name} overlaps "
                                      "another field")
                position = field.msb + 1

    @property
    def path(self):
        return f"{self.config['header_dir']}/{self.name}.h"

    def header(self, note):
        def field(f):
            return (f'groov::field<"{f.name.lower()}", {_field_type(_width(f))}, '
                    f"{f.msb}u, {f.lsb}u, {ACCESS[f.access]}>")

        def register(r):
            fields = "".join(f",\n            {field(f)}" for f in r.fields)
            return (f'        groov::reg<"{r.name.lower()}", {REGISTER_TYPES[r.size]}, '
                    f"Base + 0x{r.offset:03x}u, {ACCESS[r.access]}{fields}>")

        body = ["// The register map of one instance at Base.",
                "template <std::uintptr_t Base>",
                "constexpr auto kMap =",
                f'    groov::group<"{self.name}", {self.config["bus"]},\n'
                + ",\n".join(register(r) for r in self.registers) + ">{};",
                ""]
        for instance, base in self.instances:
            constant = _constant(instance)
            body.append(f"inline constexpr std::uintptr_t {constant}Base = 0x{base:08x}u;")
            body.append(f"inline constexpr auto {constant} = kMap<{constant}Base>;")
        includes = ("#include <groov/config.hpp>\n#include <groov/identity.hpp>\n\n"
                    "#include <cstdint>\n\n"
                    + "".join(f'#include "{i}"\n' for i in self.config["bus_includes"]))
        comment = _comment(note + "\n\n" + self.config.get("comment", ""))
        return _wrap_header(self.path, self.config["guard_prefix"], includes,
                            comment, self.namespace, "\n".join(body))


def render_interrupts(interrupts, table, device, note):
    rename, add = table.get("rename", {}), table.get("add", {})
    for name, number in add.items():
        if number in interrupts:
            raise ConfigError(f"interrupt {number} is {interrupts[number]} in "
                              f"the SVD already, not {name}")
        interrupts[number] = name
    used = set()
    names = {}
    for number, name in sorted(interrupts.items()):
        for pattern, replacement in rename.items():
            if re.fullmatch(pattern, name):
                used.add(pattern)
                name = re.sub(pattern, replacement, name)
                break
        constant = _constant(name)
        if constant in names.values():
            raise ConfigError(f"interrupt name {constant} is used twice")
        names[number] = constant
    for pattern in rename:
        if pattern not in used:
            raise ConfigError(f"'{pattern}' matches no interrupt in the SVD")
    lines = ["// NVIC priority bits implemented by the device.",
             f"inline constexpr unsigned kNvicPriorityBits = "
             f"{device.nvic_priority_bits};",
             "",
             f"enum class {table.get('name', 'Irq')} : std::uint8_t {{"]
    lines += [f"  {constant} = {number}," for number, constant in names.items()]
    lines.append("};")
    return _wrap_header(table["output"], table["guard_prefix"], "#include <cstdint>\n",
                        _comment(note + "\n\n" + table.get("comment", "")),
                        table["namespace"], "\n".join(lines))


# --- Main --------------------------------------------------------------------


def fetch_svd(svd, cache_dir):
    target = cache_dir / f"{svd['sha256']}.svd"
    if not target.exists():
        print(f"downloading {svd['url']}", file=sys.stderr)
        cache_dir.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(svd["url"]) as response:
            data = response.read()
        if (digest := hashlib.sha256(data).hexdigest()) != svd["sha256"]:
            raise ConfigError(f"{svd['url']}: SHA-256 {digest}, the config "
                              f"pins {svd['sha256']}")
        partial = target.with_suffix(".part")
        partial.write_bytes(data)
        partial.replace(target)
    return target


def verify_svd(path, sha256):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha256:
        raise ConfigError(f"{path}: SHA-256 {digest}, the config pins {sha256}")


def generate_all(config, devices, config_path):
    """{output path: text} of all maps and the interrupt header."""
    defaults = config.get("defaults", {})
    names = [config["svd"]["name"], *(e["name"] for e in config.get("svd_extra", []))]
    note = (f"Generated by tools/svd2groov from {' and '.join(names)}. Do not "
            f"edit: change {config_path} and regenerate.")
    outputs = {}
    for table in config.get("map", []):
        table = {**defaults, **table}
        try:
            groov_map = GroovMap(table, config, devices)
            outputs[groov_map.path] = groov_map.header(note)
        except ConfigError as error:
            raise ConfigError(f"map {table.get('name')}: {error}") from None
    if "interrupts" in config:
        table = {**defaults, **config["interrupts"]}
        outputs[table["output"]] = render_interrupts(
            devices.devices[0].interrupts(), table, devices.devices[0], note)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", type=pathlib.Path, required=True)
    parser.add_argument("--svd", type=pathlib.Path,
                        help="local device SVD file instead of the pinned download")
    parser.add_argument("--cache-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get(
                            "XDG_CACHE_HOME", pathlib.Path.home() / ".cache")) / "svd2groov")
    parser.add_argument("--output-root", type=pathlib.Path,
                        help="directory the output paths are relative to "
                             "(default: output_root of the config)")
    parser.add_argument("--check", action="store_true",
                        help="only compare, exit 1 if a file differs")
    args = parser.parse_args()

    stale = []
    try:
        config = tomllib.loads(args.config.read_text())
        svd = config["svd"]
        path = args.svd or fetch_svd(svd, args.cache_dir)
        verify_svd(path, svd["sha256"])
        devices = Devices([Device(path)] + [
            Device(args.config.parent / extra["file"])
            for extra in config.get("svd_extra", [])])
        project_root = args.config.parent / config.get("output_root", ".")
        output_root = args.output_root or project_root
        config_path = args.config.resolve().relative_to(
            project_root.resolve(), walk_up=True).as_posix()

        for output, text in generate_all(config, devices, config_path).items():
            target = output_root / output
            if target.exists() and target.read_text() == text:
                continue
            stale.append(output)
            if not args.check:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(text)
    except KeyError as error:
        print(f"svd2groov: missing config key {error}", file=sys.stderr)
        return 2
    except (ConfigError, OSError, ET.ParseError, tomllib.TOMLDecodeError) as error:
        print(f"svd2groov: {error}", file=sys.stderr)
        return 2

    for output in stale:
        print(f"{'stale' if args.check else 'wrote'}: {output}")
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    sys.exit(main())
