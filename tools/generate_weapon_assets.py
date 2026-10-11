from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "assets/Basic/Meshes/Effects/WeaponTrails/ElementalDesert"
TEXTURES = ROOT / "assets/Basic/Textures/effects/ElementalDesert/WeaponStyles"
PARTICLE_TAG = "RedsandParticles_Drain_v1"

@dataclass(frozen=True)
class Profile:
    name: str
    speed: float
    speed_variation: float
    radius: float
    radius_variation: float
    lifespan: float
    lifespan_variation: float
    gravity: float

PROFILES = (
    Profile("Sword", -13.0, 4.0, 9.0, 2.0, 2.40, 0.35, 48.0),
    Profile("Dagger", -17.0, 3.0, 7.0, 1.2, 1.35, 0.20, 32.0),
    Profile("WarAxe", -10.0, 5.0, 10.0, 2.0, 2.40, 0.35, 68.0),
    Profile("Mace", -8.0, 4.0, 10.5, 2.5, 2.10, 0.30, 85.0),
    Profile("Greatsword", -15.0, 4.5, 11.0, 2.0, 3.00, 0.40, 52.0),
    Profile("Battleaxe", -10.0, 5.5, 12.0, 2.5, 2.65, 0.40, 78.0),
    Profile("Warhammer", -7.0, 3.5, 12.5, 2.5, 2.20, 0.30, 100.0),
    Profile("Bow", -20.0, 3.0, 8.0, 1.5, 1.35, 0.20, 30.0),
    Profile("Crossbow", -24.0, 2.5, 8.5, 1.3, 1.10, 0.15, 42.0),
)

class FormatError(ValueError):
    pass

class Reader:
    def __init__(self, data: bytes, offset: int = 0):
        self.data = data
        self.offset = offset

    def take(self, length: int) -> bytes:
        if length < 0 or self.offset + length > len(self.data):
            raise FormatError("Field extends beyond the containing buffer")
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value

    def unpack(self, fmt: str):
        values = struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))
        return values[0] if len(values) == 1 else values

    def sized_string(self) -> str:
        return self.take(self.unpack("I")).decode("utf-8")

    def short_string(self) -> bytes:
        return self.take(self.unpack("B"))

    def finish(self):
        if self.offset != len(self.data):
            raise FormatError(f"Unexpected trailing field bytes: {len(self.data) - self.offset}")

@dataclass
class NifDocument:
    prefix: bytes
    middle: bytes
    types: tuple[str, ...]
    strings: tuple[str, ...]
    blocks: list[bytes]
    footer: bytes

    @classmethod
    def parse(cls, data: bytes) -> "NifDocument":
        newline = data.find(b"\n")
        if newline < 0 or data[:newline] != b"Gamebryo File Format, Version 20.2.0.7":
            raise FormatError("Only Gamebryo 20.2.0.7 files are supported")
        r = Reader(data, newline + 1)
        if (r.unpack("I"), r.unpack("B"), r.unpack("I")) != (0x14020007, 1, 12):
            raise FormatError("Unexpected NIF version, byte order, or user version")
        count, bs_version = r.unpack("II")
        if bs_version != 83 or not 0 < count < 10000:
            raise FormatError("Only Skyrim BS 83 block layout is supported")
        for _ in range(3):
            r.short_string()
        type_names = tuple(r.sized_string() for _ in range(r.unpack("H")))
        indices = tuple(r.unpack("H") for _ in range(count))
        if any(index >= len(type_names) for index in indices):
            raise FormatError("Invalid block type index")
        sizes_offset = r.offset
        sizes = tuple(r.unpack("I") for _ in range(count))
        middle_offset = r.offset
        string_count, maximum_length = r.unpack("II")
        strings = tuple(r.sized_string() for _ in range(string_count))
        if max((len(value.encode("utf-8")) for value in strings), default=0) != maximum_length:
            raise FormatError("String table maximum length does not match its entries")
        groups = r.unpack("I")
        r.take(groups * 4)
        data_offset = r.offset
        blocks = [r.take(size) for size in sizes]
        footer_offset = r.offset
        root_count = r.unpack("I")
        roots = tuple(r.unpack("i") for _ in range(root_count))
        if not roots or any(root < 0 or root >= count for root in roots):
            raise FormatError("Invalid root block table")
        r.finish()
        return cls(data[:sizes_offset], data[middle_offset:data_offset],
                   tuple(type_names[index] for index in indices), strings, blocks,
                   data[footer_offset:])

    def serialize(self) -> bytes:
        return (self.prefix + struct.pack("<" + "I" * len(self.blocks),
                                        *(len(block) for block in self.blocks)) +
                self.middle + b"".join(self.blocks) + self.footer)

    def string(self, index: int) -> str:
        if index == 0xFFFFFFFF:
            return ""
        if index >= len(self.strings):
            raise FormatError("Invalid string table index")
        return self.strings[index]

    def require_block(self, index: int, expected: str) -> bytes:
        if index < 0 or index >= len(self.blocks) or self.types[index] != expected:
            raise FormatError(f"Expected {expected} at referenced block {index}")
        return self.blocks[index]

    def set_block_types(self, block_types: tuple[str, ...], prune_unused: bool = False) -> None:
        r = Reader(self.prefix, self.prefix.find(b"\n") + 1)
        r.unpack("IBI")
        count_offset = r.offset
        old_count, _bs_version = r.unpack("II")
        for _ in range(3):
            r.short_string()
        table_offset = r.offset
        names = [r.sized_string() for _ in range(r.unpack("H"))]
        r.take(old_count * 2)
        r.finish()
        for name in block_types:
            if name not in names:
                names.append(name)
        if prune_unused:
            names = [name for name in names if name in block_types]
        preamble = bytearray(self.prefix[:table_offset])
        struct.pack_into("<I", preamble, count_offset, len(block_types))
        type_table = struct.pack("<H", len(names))
        for name in names:
            raw = name.encode("ascii")
            type_table += struct.pack("<I", len(raw)) + raw
        self.prefix = bytes(preamble) + type_table + struct.pack(
            "<" + "H" * len(block_types), *(names.index(name) for name in block_types))
        self.types = block_types

def object_net(doc: NifDocument, r: Reader) -> str:
    name = doc.string(r.unpack("I"))
    count = r.unpack("I")
    r.take(count * 4)
    r.unpack("i")
    return name

@dataclass(frozen=True)
class ParticleSystem:
    index: int
    shader: int
    modifiers: tuple[int, ...]

def particle_systems(doc: NifDocument) -> tuple[ParticleSystem, ...]:
    matches = []
    for index, block_type in enumerate(doc.types):
        if block_type != "NiParticleSystem":
            continue
        r = Reader(doc.blocks[index])
        name = object_net(doc, r)
        r.take(4 + 12 + 36 + 4 + 4)
        r.unpack("ii")
        materials = r.unpack("I")
        r.take(materials * 8)
        r.unpack("iB")
        shader, _alpha = r.unpack("ii")
        r.unpack("4H")
        r.unpack("B")
        modifiers = tuple(r.unpack("i") for _ in range(r.unpack("I")))
        r.finish()
        if name == PARTICLE_TAG:
            doc.require_block(shader, "BSEffectShaderProperty")
            for modifier in modifiers:
                if modifier < 0 or modifier >= len(doc.blocks):
                    raise FormatError("Invalid particle modifier reference")
            matches.append(ParticleSystem(index, shader, modifiers))
    if not matches:
        raise FormatError(f"Missing named particle system {PARTICLE_TAG}")
    return tuple(matches)

def shader_texture(doc: NifDocument, index: int) -> tuple[str, int, int]:
    r = Reader(doc.require_block(index, "BSEffectShaderProperty"))
    object_net(doc, r)
    r.unpack("II4f")
    start = r.offset
    source = r.sized_string()
    end = r.offset
    r.take(4 + 4 * 4 + 4 * 4 + 4 + 4)
    r.sized_string()
    r.finish()
    return source, start, end

@dataclass(frozen=True)
class TextureSampling:
    texture_indices: bool
    subtextures: tuple[tuple[float, float, float, float], ...]
    aspect: float
    aspect_flags: int
    speed_aspect: tuple[float, float, float]

def particle_texture_sampling(doc: NifDocument, system: ParticleSystem) -> TextureSampling:
    system_reader = Reader(doc.require_block(system.index, "NiParticleSystem"))
    object_net(doc, system_reader)
    system_reader.take(4 + 12 + 36 + 4 + 4)
    data_index = system_reader.unpack("i")
    r = Reader(doc.require_block(data_index, "NiPSysData"))
    _group, maximum_vertices, _keep, _compress, vertices = r.unpack("iHBBB")
    _vector_flags, _material_crc, normals = r.unpack("HIB")
    r.unpack("4f")
    colors, _consistency, _additional = r.unpack("BHi")
    radii, _num_active, sizes, rotations, rotation_angles, rotation_axes = r.unpack("BHBBBB")
    texture_indices = r.unpack("B")
    count = r.unpack("I")
    subtextures = tuple(r.unpack("4f") for _ in range(count))
    aspect, aspect_flags = r.unpack("fH")
    speed_aspect = r.unpack("3f")
    rotation_speeds = r.unpack("B")
    r.finish()
    booleans = (vertices, normals, colors, radii, sizes, rotations, rotation_angles,
                rotation_axes, texture_indices, rotation_speeds)
    if not maximum_vertices or any(value not in (0, 1) for value in booleans):
        raise FormatError("Unexpected particle buffer flags")
    return TextureSampling(bool(texture_indices), subtextures, aspect, aspect_flags, speed_aspect)

def shader_texture_transform(doc: NifDocument, index: int) -> tuple[int, int, tuple[float, ...], str]:
    shader_texture(doc, index)
    r = Reader(doc.require_block(index, "BSEffectShaderProperty"))
    object_net(doc, r)
    flags1, flags2 = r.unpack("II")
    uv_transform = r.unpack("4f")
    r.sized_string()
    r.take(4 + 4 * 4 + 4 * 4 + 4 + 4)
    greyscale = r.sized_string()
    r.finish()
    return flags1, flags2, uv_transform, greyscale

def modifier_header(doc: NifDocument, r: Reader, target: int) -> str:
    name = doc.string(r.unpack("I"))
    _order, actual_target, active = r.unpack("IiB")
    if actual_target != target or active not in (0, 1):
        raise FormatError("Particle modifier has an unexpected target or active value")
    return name

def emitter_fields(doc: NifDocument, index: int, target: int) -> dict[str, tuple[int, float]]:
    r = Reader(doc.require_block(index, "NiPSysMeshEmitter"))
    modifier_header(doc, r, target)
    fields = {}
    for key in ("speed", "speed_variation", "declination", "declination_variation",
                "planar_angle", "planar_angle_variation"):
        offset = r.offset
        fields[key] = offset, r.unpack("f")
    r.unpack("4f")
    for key in ("radius", "radius_variation", "lifespan", "lifespan_variation"):
        offset = r.offset
        fields[key] = offset, r.unpack("f")
    count = r.unpack("I")
    for _ in range(count):
        mesh = r.unpack("i")
        if mesh < 0 or mesh >= len(doc.blocks) or doc.types[mesh] not in ("NiTriShape", "NiTriStrips", "NiNode"):
            raise FormatError("Unexpected emitter geometry reference")
    r.unpack("II3f")
    r.finish()
    return fields

def gravity_fields(doc: NifDocument, index: int, target: int) -> dict[str, tuple[int, float]]:
    r = Reader(doc.require_block(index, "NiPSysGravityModifier"))
    modifier_header(doc, r, target)
    r.unpack("i3ff")
    strength_offset = r.offset
    strength = r.unpack("f")
    r.unpack("IffB")
    r.finish()
    return {"gravity": (strength_offset, strength)}

@dataclass(frozen=True)
class ParticleController:
    index: int
    next: int
    flags: int
    timing: tuple[float, float, float, float]
    target: int
    interpolator: int = -1
    modifier_name: int = 0xFFFFFFFF

MOTION_CONTROLLERS = (
    ("NiPSysEmitterSpeedCtlr", "speed"),
    ("NiPSysEmitterInitialRadiusCtlr", "radius"),
)

def particle_controllers(doc: NifDocument, system: ParticleSystem) -> tuple[ParticleController, ...]:
    r = Reader(doc.blocks[system.index])
    r.unpack("I")
    r.take(r.unpack("I") * 4)
    index = r.unpack("i")
    result = []
    visited = set()
    while index != -1:
        if index in visited or index < 0 or index >= len(doc.blocks):
            raise FormatError("Invalid or cyclic particle controller chain")
        visited.add(index)
        block_type = doc.types[index]
        if block_type not in ("NiPSysEmitterCtlr", "NiPSysUpdateCtlr", *(name for name, _ in MOTION_CONTROLLERS)):
            raise FormatError(f"Unexpected particle controller type {block_type}")
        r = Reader(doc.blocks[index])
        next_index, flags = r.unpack("iH")
        timing = r.unpack("4f")
        target = r.unpack("i")
        if target != system.index:
            raise FormatError("Particle controller targets a different system")
        interpolator, modifier_name = -1, 0xFFFFFFFF
        if block_type != "NiPSysUpdateCtlr":
            interpolator, modifier_name = r.unpack("iI")
            doc.require_block(interpolator, "NiFloatInterpolator")
            doc.string(modifier_name)
            if block_type == "NiPSysEmitterCtlr":
                r.unpack("i")
        r.finish()
        result.append(ParticleController(index, next_index, flags, timing, target, interpolator, modifier_name))
        index = next_index
    return tuple(result)

def ensure_motion_controllers(doc: NifDocument) -> None:
    for system in particle_systems(doc):
        chain = particle_controllers(doc, system)
        birth = next((controller for controller in chain if doc.types[controller.index] == "NiPSysEmitterCtlr"), None)
        emitters = [index for index in system.modifiers if doc.types[index] == "NiPSysMeshEmitter"]
        if not birth or len(emitters) != 1 or not chain:
            raise FormatError("Motion controllers require one existing mesh emitter and birth controller")
        emitter = emitters[0]
        fields = emitter_fields(doc, emitter, system.index)
        modifier_name = Reader(doc.blocks[emitter]).unpack("I")
        if birth.modifier_name != modifier_name:
            raise FormatError("Birth controller modifier name does not match the emitter")
        motion = [controller for controller in chain
                  if doc.types[controller.index] in (name for name, _ in MOTION_CONTROLLERS)]
        existing = {doc.types[controller.index]: controller for controller in motion}
        if len(existing) not in (0, len(MOTION_CONTROLLERS)) or len(motion) != len(existing):
            raise FormatError("Incomplete speed/radius controller pair")
        original_chain = [controller for controller in chain if controller not in motion]
        if tuple(doc.types[controller.index] for controller in original_chain) != ("NiPSysEmitterCtlr", "NiPSysUpdateCtlr"):
            raise FormatError("Motion controller insertion expects the original birth/update chain")
        if not existing:
            first = len(doc.blocks)
            appended_types = tuple(block_type for name, _ in MOTION_CONTROLLERS
                                   for block_type in (name, "NiFloatInterpolator"))
            doc.set_block_types(doc.types + appended_types)
            motion_indices = {}
            for offset, (name, key) in enumerate(MOTION_CONTROLLERS):
                index = first + offset * 2
                motion_indices[name] = index
                next_index = index + 2 if offset + 1 < len(MOTION_CONTROLLERS) else original_chain[-1].index
                controller = struct.pack("<iH4fiiI", next_index, birth.flags, *birth.timing,
                                         system.index, index + 1, modifier_name)
                doc.blocks.extend((controller, struct.pack("<fi", fields[key][1], -1)))
        else:
            motion_indices = {name: existing[name].index for name, _ in MOTION_CONTROLLERS}
            for name, key in MOTION_CONTROLLERS:
                controller = existing[name]
                if controller.modifier_name != modifier_name or controller.flags != birth.flags or controller.timing != birth.timing:
                    raise FormatError("Existing motion controller no longer matches its birth controller")
                interpolator = Reader(doc.require_block(controller.interpolator, "NiFloatInterpolator"))
                interpolator.unpack("f")
                if interpolator.unpack("i") != -1:
                    raise FormatError("Motion controller interpolators must be constant, with no shared key data")
                interpolator.finish()
                doc.blocks[controller.interpolator] = struct.pack("<fi", fields[key][1], -1)
        desired_chain = [birth.index, *(motion_indices[name] for name, _ in MOTION_CONTROLLERS), original_chain[-1].index]
        for index, next_index in zip(desired_chain, desired_chain[1:] + [-1]):
            controller = bytearray(doc.blocks[index])
            struct.pack_into("<i", controller, 0, next_index)
            doc.blocks[index] = bytes(controller)

def variant_bytes(data: bytes, profile: Profile) -> bytes:
    doc = NifDocument.parse(data)
    for system in particle_systems(doc):
        sampling = particle_texture_sampling(doc, system)
        if (not sampling.texture_indices or sampling.subtextures != ((0.0, 1.0, 0.0, 1.0),) or
                sampling.aspect_flags != 0):
            raise FormatError("Weapon shapes require a single full-texture quad without aspect transforms")
        flags1, _flags2, transform, greyscale = shader_texture_transform(doc, system.shader)
        if flags1 & 0x30 or transform != (0.0, 0.0, 1.0, 1.0) or greyscale:
            raise FormatError("Weapon shapes require neutral full-texture UVs without a greyscale palette")
        source, start, end = shader_texture(doc, system.shader)
        if source.casefold().replace("/", "\\") != "textures\\effects\\elementaldesert\\circle_05.dds":
            raise FormatError("The source particle texture is not the expected original circle_05.dds")
        texture = f"textures\\effects\\ElementalDesert\\WeaponStyles\\{profile.name}.dds".encode("ascii")
        shader = doc.blocks[system.shader]
        doc.blocks[system.shader] = shader[:start] + struct.pack("<I", len(texture)) + texture + shader[end:]
        emitter_count = gravity_count = 0
        for index in system.modifiers:
            if doc.types[index] == "NiPSysMeshEmitter":
                emitter_count += 1
                fields = emitter_fields(doc, index, system.index)
                block = bytearray(doc.blocks[index])
                for key in ("speed", "speed_variation", "radius", "radius_variation", "lifespan", "lifespan_variation"):
                    struct.pack_into("<f", block, fields[key][0], getattr(profile, key))
                doc.blocks[index] = bytes(block)
            elif doc.types[index] == "NiPSysGravityModifier":
                gravity_count += 1
                fields = gravity_fields(doc, index, system.index)
                block = bytearray(doc.blocks[index])
                struct.pack_into("<f", block, fields["gravity"][0], profile.gravity)
                doc.blocks[index] = bytes(block)
        if emitter_count != 1 or gravity_count != 1:
            raise FormatError("Expected one mesh emitter and one gravity modifier per named particle system")
    ensure_motion_controllers(doc)
    output = doc.serialize()

    check = NifDocument.parse(output)
    for system in particle_systems(check):
        shader_texture(check, system.shader)
    return output

def smooth_edge(distance: float, softness: float = 0.024) -> float:
    t = min(1.0, max(0.0, 0.5 - distance / softness))
    solid = t * t * (3.0 - 2.0 * t)
    glow = 0.18 * math.exp(-max(0.0, distance) ** 2 / 0.0020)
    return min(1.0, max(solid, glow))

def diamond(x: float, y: float, width: float, height: float) -> float:
    return (abs(x) / width + abs(y) / height - 1.0) * min(width, height)

def segment_distance(x: float, y: float, ax: float, ay: float, bx: float, by: float) -> float:
    dx, dy = bx - ax, by - ay
    t = min(1.0, max(0.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - ax - t * dx, y - ay - t * dy)

def arc_distance(x: float, y: float, radius: float, thickness: float, angle: float) -> float:
    y += 0.12
    theta = math.atan2(y, x)
    start, stop = angle, math.pi - angle
    if start <= theta <= stop:
        distance = abs(math.hypot(x, y) - radius)
    else:
        distance = min(math.hypot(x - radius * math.cos(start), y - radius * math.sin(start)),
                       math.hypot(x - radius * math.cos(stop), y - radius * math.sin(stop)))
    return distance - thickness

def shape_alpha(name: str, x: float, y: float) -> float:
    facet = 1.0
    if name == "Sword":
        distance = diamond(x, y, 0.17, 0.81)
        facet = 0.78 + 0.22 * (1.0 - min(1.0, abs(x) / 0.17))
    elif name == "Dagger":
        distance = diamond(x, y, 0.075, 0.57)
    elif name == "WarAxe":
        distance = arc_distance(x, y, 0.57, 0.070, 0.14)
        facet = 0.82 + 0.18 * max(0.0, y)
    elif name == "Mace":
        radius = math.hypot(x, y)
        theta = math.atan2(y, x)
        distance = radius - (0.38 + 0.025 * math.cos(6.0 * theta))
        facet = 0.65 + 0.35 * max(0.0, 1.0 - math.hypot(x + 0.13, y - 0.13) / 0.55)
    elif name == "Greatsword":
        distance = diamond(x, y, 0.25, 0.85)
        facet = 0.58 + 0.42 * max(0.0, 1.0 - abs(x + 0.045) / 0.20)
    elif name == "Battleaxe":
        distance = arc_distance(x, y, 0.58, 0.145, 0.04)
        facet = 0.60 + 0.40 * max(0.0, min(1.0, (y + 0.12) / 0.6))
    elif name == "Warhammer":
        distance = max((abs(x) + abs(y) - 0.70) * 0.707, abs(y) - 0.45)
        facet = 0.60 + (0.24 if x < 0.0 else 0.0) + (0.16 if y > 0.0 else 0.0)
    elif name == "Bow":

        distance = min(segment_distance(x, y, -0.32, 0.19, 0.0, 0.72) - 0.030,
                       segment_distance(x, y, 0.32, 0.19, 0.0, 0.72) - 0.030,
                       diamond(x, y + 0.07, 0.050, 0.65))
    elif name == "Crossbow":

        distance = min(diamond(x, y - 0.20, 0.28, 0.43),
                       diamond(x, y + 0.26, 0.070, 0.36))
        facet = 0.68 + 0.32 * (1.0 - min(1.0, abs(x) / 0.28))
    else:
        raise ValueError(name)
    edge_mask = max(0.0, min(1.0, (0.94 - max(abs(x), abs(y))) / 0.05))
    return smooth_edge(distance) * facet * edge_mask

def texture_pixels(profile: Profile, size: int = 128) -> bytes:
    if size < 128 or size & (size - 1):
        raise ValueError("Texture size must be a power of two, at least 128")
    pixels = bytearray()
    for row in range(size):
        y = 1.0 - 2.0 * (row + 0.5) / size
        for column in range(size):
            x = 2.0 * (column + 0.5) / size - 1.0
            alpha = round(255.0 * shape_alpha(profile.name, x, y))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)

def mip_chain(rgba: bytes, size: int) -> tuple[bytes, ...]:
    levels = [rgba]
    while size > 1:
        reduced = size // 2
        source = levels[-1]
        output = bytearray()
        for y in range(reduced):
            for x in range(reduced):
                alpha = sum(source[((2 * y + dy) * size + 2 * x + dx) * 4 + 3]
                            for dy in range(2) for dx in range(2))

                if x in (0, reduced - 1) or y in (0, reduced - 1):
                    alpha = 0
                output.extend((255, 255, 255, (alpha + 2) // 4))
        levels.append(bytes(output))
        size = reduced
    return tuple(levels)

def dds_bytes(rgba: bytes, size: int = 128) -> bytes:
    levels = mip_chain(rgba, size)

    header = [124, 0x0002100F, size, size, size * 4, 0, len(levels)] + [0] * 11
    header += [32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000]
    header += [0x00401008, 0, 0, 0, 0]
    payload = bytearray()
    for level in levels:
        for offset in range(0, len(level), 4):
            red, green, blue, alpha = level[offset:offset + 4]
            payload.extend((blue, green, red, alpha))
    return b"DDS " + struct.pack("<31I", *header) + payload

def decode_dds_top(data: bytes) -> tuple[int, bytes]:
    if len(data) < 128 or data[:4] != b"DDS ":
        raise FormatError("Missing DDS header")
    header = struct.unpack("<31I", data[4:128])
    size = header[2]
    if (header[0] != 124 or header[3] != size or size < 1 or size & (size - 1) or
            header[4] != size * 4 or header[6] != size.bit_length() or
            header[18:26] != (32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)):
        raise FormatError("Expected square BGRA8 DDS with its complete mip chain")
    expected_size = 128 + sum(max(1, size >> mip) ** 2 * 4 for mip in range(header[6]))
    if len(data) != expected_size:
        raise FormatError("DDS mip payload length does not match its header")
    rgba = bytearray()
    for offset in range(128, 128 + size * size * 4, 4):
        blue, green, red, alpha = data[offset:offset + 4]
        rgba.extend((red, green, blue, alpha))
    return size, bytes(rgba)

def png_bytes(width: int, height: int, pixels: bytes, description: str = "") -> bytes:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    scanlines = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    metadata = chunk(b"tEXt", b"Description\0" + description.encode("latin-1")) if description else b""
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + metadata +
            chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))

FONT = {
    "A":("01110","10001","10001","11111","10001","10001","10001"),
    "B":("11110","10001","10001","11110","10001","10001","11110"),
    "C":("01111","10000","10000","10000","10000","10000","01111"),
    "D":("11110","10001","10001","10001","10001","10001","11110"),
    "E":("11111","10000","10000","11110","10000","10000","11111"),
    "F":("11111","10000","10000","11110","10000","10000","10000"),
    "G":("01111","10000","10000","10111","10001","10001","01110"),
    "H":("10001","10001","10001","11111","10001","10001","10001"),
    "I":("111","010","010","010","010","010","111"),
    "L":("10000","10000","10000","10000","10000","10000","11111"),
    "M":("10001","11011","10101","10101","10001","10001","10001"),
    "O":("01110","10001","10001","10001","10001","10001","01110"),
    "R":("11110","10001","10001","11110","10100","10010","10001"),
    "S":("01111","10000","10000","01110","00001","00001","11110"),
    "T":("11111","00100","00100","00100","00100","00100","00100"),
    "W":("10001","10001","10001","10101","10101","10101","01010"),
    "X":("10001","10001","01010","00100","01010","10001","10001"),
    "N":("10001","11001","10101","10011","10001","10001","10001"),
    "P":("11110","10001","10001","11110","10000","10000","10000"),
    "U":("10001","10001","10001","10001","10001","10001","01110"),
    "Y":("10001","10001","01010","00100","00100","00100","00100"),
    "V":("10001","10001","10001","10001","10001","01010","00100"),
    " ":("000","000","000","000","000","000","000"),
}

def preview_bytes(texture_directory: Path = TEXTURES) -> bytes:
    width, height = 750, 710
    pixels = bytearray(bytes((17, 22, 31, 255)) * (width * height))

    def pixel(x: int, y: int, color: tuple[int, int, int, int]):
        if 0 <= x < width and 0 <= y < height:
            offset = (y * width + x) * 4
            pixels[offset:offset + 4] = bytes(color)

    def label(text: str, x: int, y: int, scale: int = 2):
        for letter in text.upper():
            glyph = FONT[letter]
            for row, line in enumerate(glyph):
                for column, bit in enumerate(line):
                    if bit == "1":
                        for dy in range(scale):
                            for dx in range(scale):
                                pixel(x + column * scale + dx, y + row * scale + dy, (202, 212, 228, 255))
            x += (len(glyph[0]) + 1) * scale

    label("DDS TEXTURE PREVIEW", 24, 18, 2)
    label("ACTUAL DDS ALPHA AND RGB", 24, 42, 1)
    for index, profile in enumerate(PROFILES):
        left, top = (index % 3) * 250, 70 + (index // 3) * 200
        size, rgba = decode_dds_top((texture_directory / f"{profile.name}.dds").read_bytes())
        for y in range(160):
            for x in range(160):
                source_x, source_y = x * size // 160, y * size // 160
                source_offset = (source_y * size + source_x) * 4
                alpha = rgba[source_offset + 3] / 255.0
                background = (25, 31, 42)
                foreground = rgba[source_offset:source_offset + 3]
                color = tuple(round(background[c] * (1.0 - alpha) + foreground[c] * alpha) for c in range(3))
                pixel(left + 45 + x, top + y, color + (255,))
        text_width = sum(len(FONT[letter][0]) + 1 for letter in profile.name.upper()) * 2
        label(profile.name, left + (250 - text_width) // 2, top + 172)
    label("NOT A GAME SCREENSHOT", 24, 686, 1)
    return png_bytes(width, height, bytes(pixels),
                     "Actual DDS RGB and alpha decoded from generated assets. Not a game screenshot.")

def generate() -> tuple[int, int]:
    sources = sorted(BASE.glob("*.nif"))
    if len(sources) != 10:
        raise FormatError(f"Expected ten base colour NIFs, found {len(sources)}")
    source_data = {}
    for path in sources:
        doc = NifDocument.parse(path.read_bytes())
        for system in particle_systems(doc):
            for index in system.modifiers:
                if doc.types[index] == "NiPSysMeshEmitter":
                    fields = emitter_fields(doc, index, system.index)

                    block = bytearray(doc.blocks[index])
                    struct.pack_into("<f", block, fields["radius_variation"][0],
                                     min(fields["radius_variation"][1], fields["radius"][1] * 0.2))
                    doc.blocks[index] = bytes(block)
        ensure_motion_controllers(doc)
        data = doc.serialize()
        NifDocument.parse(data)
        path.write_bytes(data)
        source_data[path.name] = data
    TEXTURES.mkdir(parents=True, exist_ok=True)
    for profile in PROFILES:
        mesh_folder = BASE / "WeaponStyles" / profile.name
        mesh_folder.mkdir(parents=True, exist_ok=True)
        pixels = texture_pixels(profile)
        (TEXTURES / f"{profile.name}.dds").write_bytes(dds_bytes(pixels))
        for filename, data in source_data.items():
            (mesh_folder / filename).write_bytes(variant_bytes(data, profile))
    preview = ROOT / "output/weapon-particle-styles.png"
    preview.parent.mkdir(parents=True, exist_ok=True)
    preview.write_bytes(preview_bytes())
    return len(source_data) * len(PROFILES), len(PROFILES)

def inspect_sources() -> None:
    for path in sorted(BASE.glob("*.nif")):
        doc = NifDocument.parse(path.read_bytes())
        print(path.name, len(doc.blocks), "blocks", len(doc.strings), "strings")
        for system in particle_systems(doc):
            print(" particle", system.index, "shader", system.shader, shader_texture(doc, system.shader))
            print(" texture UV", particle_texture_sampling(doc, system))
            print(" shader sampling", shader_texture_transform(doc, system.shader))
            for index in system.modifiers:
                if doc.types[index] == "NiPSysMeshEmitter":
                    print(" emitter", index, emitter_fields(doc, index, system.index))
                elif doc.types[index] == "NiPSysGravityModifier":
                    print(" gravity", index, gravity_fields(doc, index, system.index))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect", action="store_true", help="Report source NIF fields without writing assets")
    args = parser.parse_args()
    if args.inspect:
        inspect_sources()
    else:
        meshes, textures = generate()
        print(f"Generated {meshes} NIF variants, {textures} BGRA8 DDS textures with full mip chains")
        print(ROOT / "output/weapon-particle-styles.png")
