import io
import math
import struct
import sys

from PIL import Image


def tile_id(z, x, y):
    if z == 0:
        return 0
    acc = ((1 << (2 * z)) - 1) // 3
    n = 1 << z
    d = 0
    s = n // 2
    while s > 0:
        rx = 1 if (x & s) else 0
        ry = 1 if (y & s) else 0
        d += s * s * ((3 * rx) ^ ry)
        if ry == 0:
            if rx == 1:
                x = n - 1 - x
                y = n - 1 - y
            x, y = y, x
        s //= 2
    return acc + d


def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def tile_lat(z, row):
    n = math.pi * (1 - 2 * row / (1 << z))
    return math.degrees(math.atan(math.sinh(n)))


def tile_lon(z, col):
    return col / (1 << z) * 360.0 - 180.0


def terrarium(metres):
    v = round((metres + 32768.0) * 256.0)
    return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def tile_png(z, x, y, box, metres, fmt):
    south, west, north, east = box
    img = Image.new("RGBA", (256, 256))
    px = img.load()
    rgb = terrarium(metres)
    for r in range(256):
        lat = tile_lat(z, y + (r + 0.5) / 256)
        for c in range(256):
            lon = tile_lon(z, x + (c + 0.5) / 256)
            inside = south <= lat <= north and west <= lon <= east
            px[c, r] = (rgb[0], rgb[1], rgb[2], 255) if inside else (0, 0, 0, 0)
    buf = io.BytesIO()
    if fmt == "webp":
        img.save(buf, "WEBP", lossless=True)
    else:
        img.save(buf, "PNG")
    return buf.getvalue()


def tile_xy(z, lat, lon):
    n = 1 << z
    x = int((lon + 180.0) / 360.0 * n)
    r = math.radians(lat)
    y = int((1 - math.log(math.tan(r) + 1 / math.cos(r)) / math.pi) / 2 * n)
    return min(max(x, 0), n - 1), min(max(y, 0), n - 1)


def main():
    out, metres, maxzoom, fmt = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    box = (44.0, -112.0, 45.5, -110.0)
    tiles = []
    for z in range(0, maxzoom + 1):
        x0, y0 = tile_xy(z, box[2], box[1])
        x1, y1 = tile_xy(z, box[0], box[3] - 1e-9)
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                tiles.append((tile_id(z, x, y), tile_png(z, x, y, box, metres, fmt)))
    tiles.sort()
    data = bytearray()
    entries = []
    for tid, blob in tiles:
        entries.append((tid, len(data), len(blob)))
        data += blob
    d = bytearray(varint(len(entries)))
    last = 0
    for tid, _, _ in entries:
        d += varint(tid - last)
        last = tid
    for _ in entries:
        d += varint(1)
    for _, _, length in entries:
        d += varint(length)
    for _, offset, _ in entries:
        d += varint(offset + 1)
    meta = b"{}"
    root_off = 127
    meta_off = root_off + len(d)
    leaf_off = meta_off + len(meta)
    data_off = leaf_off
    header = bytearray(b"PMTiles") + bytes([3])
    header += struct.pack("<QQQQQQQQ", root_off, len(d), meta_off, len(meta), leaf_off, 0, data_off, len(data))
    header += struct.pack("<QQQ", len(entries), len(entries), len(entries))
    tile_type = 4 if fmt == "webp" else 2
    header += bytes([1, 1, 1, tile_type, 0, maxzoom])
    header += struct.pack("<iiii", int(box[1] * 1e7), int(box[0] * 1e7), int(box[3] * 1e7), int(box[2] * 1e7))
    header += bytes([0]) + struct.pack("<ii", int(-111.0 * 1e7), int(44.75 * 1e7))
    assert len(header) == 127
    with open(out, "wb") as f:
        f.write(header + d + meta + data)


if __name__ == "__main__":
    main()
