
from pathlib import Path
import json
import re
import struct
import sys

if sys.version_info < (3, 11):
    raise SystemExit("ImpactVFX packaging requires Python 3.11+ (standard-library tomllib)")
import tomllib


def check(value, message):
    if not value:
        raise ValueError(message)


def u32(data, pos=0): return struct.unpack_from("<I", data, pos)[0]


def nif(data):
    pos = data.find(b"\n") + 1
    check(data[:pos] == b"Gamebryo File Format, Version 20.2.0.7\n", "Wrong NIF signature")
    check(struct.unpack_from("<IBI", data, pos) == (0x14020007, 1, 12), "Wrong NIF version")
    pos += 9
    count, version = struct.unpack_from("<II", data, pos); pos += 8
    check(0 < count < 10000 and version in (83, 100), "Unsupported NIF layout")
    for _ in range(3): pos += 1 + data[pos]
    types = struct.unpack_from("<H", data, pos)[0]; pos += 2
    names = []
    for _ in range(types):
        length = u32(data, pos); pos += 4
        names.append(data[pos:pos + length].decode()); pos += length
    indices = struct.unpack_from("<" + "H" * count, data, pos); pos += count * 2
    check(all(index < types for index in indices), "Invalid NIF block type")
    sizes = struct.unpack_from("<" + "I" * count, data, pos); pos += count * 4
    strings, longest = struct.unpack_from("<II", data, pos); pos += 8
    lengths = []
    for _ in range(strings):
        length = u32(data, pos); pos += 4
        check(pos + length <= len(data), "Truncated NIF string")
        data[pos:pos + length].decode("utf-8"); lengths.append(length); pos += length
    check(max(lengths, default=0) == longest, "NIF string maximum differs")
    groups = u32(data, pos); pos += 4 + groups * 4 + sum(sizes)
    roots = u32(data, pos); pos += 4
    check(0 < roots < 10000 and pos + roots * 4 == len(data), "NIF block/footer size mismatch")
    check(all(0 <= index < count for index in struct.unpack_from("<" + "i" * roots, data, pos)), "NIF root out of bounds")
    return [names[index] for index in indices]


def main(stage):
    nif_paths = list((stage / "Meshes/StellarImpact").rglob("*.nif"))
    families = ["Hit/StoneBurst.nif", "Hit/MetalBurst.nif", "Slash/AzureCrescentSlash.nif", "Slash/GoldBladeSlash.nif",
                "Block/NormalBlock.nif", "Block/TimedSparks.nif", "Block/TimedFlare.nif", "Block/PerfectRing.nif", "Block/SimpleTimed.nif", "Refraction/SoftRefraction.nif"]
    expected_nifs = {"Block/StarRing.nif"} | set(families)
    check({p.relative_to(stage / "Meshes/StellarImpact").as_posix() for p in nif_paths} == expected_nifs,
          "Expected ten shared base models and one private star-ring model; legacy preset copies must be omitted")
    texture_refs = set()
    for path in nif_paths:
        data = path.read_bytes(); types = nif(data)
        if path.name == "GoldBladeSlash.nif":
            check(("BSTriShape" in types or {"NiTriShape", "NiTriShapeData"} <= set(types)) and
                  not set(types) & {"BSValueNode", "NiControllerManager"}, "Native slash must keep its measured geometry plane")
        texture_refs.update(m.decode().replace("\\", "/") for m in re.findall(rb"(?i)textures[\\/]StellarImpact[\\/][A-Za-z0-9_\\/.-]+\.dds", data))
    for texture in texture_refs: check((stage / texture).is_file(), "Missing packaged texture: " + texture)
    textures = list((stage / "Textures/StellarImpact").rglob("*.dds"))
    retained = set()
    check({p.lower() for p in texture_refs | retained} <=
          {p.relative_to(stage).as_posix().lower() for p in textures}, "Unresolved VFX texture")
    for path in textures:
        data = path.read_bytes()
        check(data[:4] == b"DDS " and u32(data, 4) == 124 and u32(data, 76) == 32, "Invalid DDS header")
        check(0 < u32(data, 12) <= 8192 and 0 < u32(data, 16) <= 8192 and len(data) > 128, "Invalid DDS dimensions/payload")
    check(not any(p.suffix.lower() in (".esp", ".esl", ".esm") for p in stage.rglob("*") if p.is_file()),
          "Redsand must not package ESP/ESL/ESM files")
    check(not any(p.suffix.lower() in (".wav", ".xwm", ".fuz") for p in stage.rglob("*") if p.is_file()),
          "Redsand must not package audio files")
    document = tomllib.loads((stage / "SKSE/Plugins/RedsandImpactVFX.toml").read_text(encoding="utf-8"))
    routes = document["Route"]
    check(document["schemaVersion"] == 1 and len(routes) == 26, "Wrong route schema/count")
    check({t: sum(r["type"] == t for r in routes) for t in ("IPCT", "EXPL", "PROJ")} ==
          {"IPCT": 26, "EXPL": 0, "PROJ": 0}, "Wrong runtime form types")
    by_form = {(r["plugin"].lower(), r["form"]): r for r in routes}
    check(len(by_form) == len(routes), "Duplicate runtime form route")
    for route in document["Route"]:
        check(route['plugin'] == 'Skyrim.esm', "External combat form override remains")
        check(len(route["models"]) == 64, "Wrong preset count")
        for i, model in enumerate(route["models"]):
            check(model == route['baseModel'], "Legacy slots must share the same continuous-control model")
            check((stage / "Meshes" / model.replace("\\", "/")).is_file(), "Missing shared model")
        if "offModel" in route: check((stage / "Meshes" / route["offModel"].replace("\\", "/")).is_file(), "Missing off effect")
        if route["type"] != "IPCT": check("duration" not in route and "originalDuration" not in route, "Non-IPCT route owns gameplay DATA")
    check(all(r["plugin"].lower() != "stellar impact.esp" for r in routes), "Owned form route remains")
    native = document["Native"]
    check(len(native) == 5 and {r["id"] for r in native} == {"Hit", "Stone", "Slash", "Block", "Wave"},
          "Missing native effect layer")
    expected = {"Hit": ("Hit", "Hit/MetalBurst.nif"), "Stone": ("Hit", "Hit/StoneBurst.nif"),
                "Slash": ("Slash", "Slash/GoldBladeSlash.nif"), "Block": ("Block", "Block/NormalBlock.nif"),
                "Wave": ("Hit", "Refraction/SoftRefraction.nif")}
    for layer in native:
        category, filename = expected[layer["id"]]
        check(set(layer) == {"id", "category", "model", "duration"}, "Unexpected native layer field")
        check(layer["category"] == category and layer["model"].replace("\\", "/") ==
              "StellarImpact/" + filename and layer["duration"] == .85,
              "Native layer path/category/duration changed")
        check((stage / "Meshes" / layer["model"].replace("\\", "/")).is_file(), "Missing native layer model")
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
    from runtime_payload import is_runtime_file
    files = {p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()}
    check(all(is_runtime_file(name) for name in files), "External module or non-runtime file remains")
    check({name for name in files if name.endswith('.dll')} <= {'SKSE/Plugins/Precision.dll'}, "Bundled combat DLL remains")
    print(f"PASS: ImpactVFX package: {len(nif_paths)} NIFs, {len(textures)} DDS, 26 vanilla routes, five native layers, zero plugins/audio")


if __name__ == "__main__": main(Path(sys.argv[1]))
