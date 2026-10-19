import argparse
import struct
import hashlib
from pathlib import Path
from PIL import Image
import math

ROOT = Path(__file__).resolve().parents[1]
THREADS_SOURCE = ROOT / 'resources/SourceTextures/effekseer-tx_slash01_256.png'

def sat(x):
    return min(1.0, max(0.0, x))

def smooth(d, soft=0.018):
    t = sat(0.5 - d / soft)
    return t * t * (3 - 2 * t)

def shape(kind, x, y):
    if kind == 'Crescent':
        x, y = (0.819 * x + 0.574 * y, -0.574 * x + 0.819 * y)
        theta = math.atan2(y + 0.08, x)
        radius = math.hypot(x, y + 0.08)
        start, stop = (0.12, math.pi - 0.12)
        if start < theta < stop:
            taper = math.sin((theta - start) / (stop - start) * math.pi) ** 0.7
            primary = smooth(abs(radius - 0.68) - (0.014 + 0.046 * taper), 0.012) * taper
            inner = smooth(abs(radius - 0.585) - 0.009, 0.01) * taper * (0.32 if 1.0 < theta < 1.17 or 1.94 < theta < 2.13 else 0.8)
            return (max(primary, inner), 0.9 + 0.1 * taper)
        return (0.0, 1.0)
    if kind == 'RedTrace':
        x, y = (0.819 * x + 0.574 * y, -0.574 * x + 0.819 * y)
        a = 0.0
        for dx, dy, length, width in [(-0.13, 0.0, 0.74, 0.018), (0.1, -0.05, 0.54, 0.012), (0.26, 0.12, 0.33, 0.01)]:
            a = max(a, smooth((abs(x - dx) / width + abs(y - dy) / length - 1.0) * width, 0.009))
        return (a, 1.0)
    raise ValueError(kind)

def pixels(kind, size=256):
    out = bytearray()
    for row in range(size):
        y = 1.0 - 2 * (row + 0.5) / size
        for col in range(size):
            x = 2 * (col + 0.5) / size - 1.0
            alpha, value = shape(kind, x, y)
            alpha *= sat((0.97 - max(abs(x), abs(y))) / 0.035)
            c = round(255 * value)
            out.extend((c, c, c, round(255 * sat(alpha))))
    return bytes(out)

def write_dds_rgba(path, width, height, pixels):
    if width < 1 or height < 1 or width & width - 1 or height & height - 1 or (len(pixels) != width * height * 4):
        raise ValueError('Power-of-two dimensions / RGBA length required')
    levels = [bytes(pixels)]
    w, h = (width, height)
    while w > 1 or h > 1:
        nw, nh = (max(1, w // 2), max(1, h // 2))
        dst = bytearray()
        src = levels[-1]
        for y in range(nh):
            for x in range(nw):
                offsets = [(min(h - 1, y * 2 + dy) * w + min(w - 1, x * 2 + dx)) * 4 for dy in range(2) for dx in range(2)]
                samples = [src[offset:offset + 4] for offset in offsets]
                sa = sum((p[3] for p in samples))
                rgb = [round(sum((p[c] * p[3] for p in samples)) / sa) if sa else 255 for c in range(3)]
                dst.extend((*rgb, round(sa / 4)))
        levels.append(bytes(dst))
        w, h = (nw, nh)
    header = [124, 135183, height, width, width * 4, 0, len(levels)] + [0] * 11
    header += [32, 65, 0, 32, 16711680, 65280, 255, 4278190080, 4198408, 0, 0, 0, 0]
    data = bytearray(b'DDS ' + struct.pack('<31I', *header))
    for level in levels:
        for i in range(0, len(level), 4):
            r, g, b, a = level[i:i + 4]
            data.extend((b, g, r, a))
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return {'path': str(p), 'width': width, 'height': height, 'mip_count': len(levels), 'bytes': len(data), 'format': 'BGRA8_UNORM'}

def generate(destination, threads_source=THREADS_SOURCE):
    destination = Path(destination)
    results = {}
    for name in ('Crescent', 'RedTrace'):
        path = destination / (name + '.dds')
        write_dds_rgba(path, 256, 256, pixels(name))
        results[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    row = bytearray()
    for x in range(256):
        distance = min(x, 255 - x)
        alpha = min(1.0, max(0.0, (distance - 2) / 9.0))
        row.extend((255, 255, 255, round(alpha * 255)))
    path = destination / 'GoldSweepMask.dds'
    write_dds_rgba(path, 256, 4, bytes(row) * 4)
    results['GoldSweepMask'] = hashlib.sha256(path.read_bytes()).hexdigest()
    if threads_source is not None:
        source = Path(threads_source)
        if hashlib.sha256(source.read_bytes()).hexdigest() != '89294f59052c63b97dfec9b3a043afa0e9f28037d4c71701628ff0f2df3d4544':
            raise ValueError('Unexpected source texture')
        with Image.open(source) as image:
            image = image.convert('RGBA')
            raw = image.tobytes()
            size = image.size
        path = destination / 'GoldCutThreads.dds'
        write_dds_rgba(path, *size, raw)
        results['GoldCutThreads'] = hashlib.sha256(path.read_bytes()).hexdigest()
    return results

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--threads-source', type=Path, default=THREADS_SOURCE)
    args = parser.parse_args()
    for name, value in generate(args.output, args.threads_source).items():
        print(name + ' ' + value)
if __name__ == '__main__':
    main()
