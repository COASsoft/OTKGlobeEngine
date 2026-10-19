import struct, math, os, sys, gzip
root = sys.argv[1]
def height(lat, lon):
    return 1500 + 300 * math.cos((lat - 47) * 20) * math.cos((lon + 122) * 20)
def zz(v): return (v << 1) ^ (v >> 31)
def tile(z, x, y):
    span = 180.0 / (1 << z)
    w = -180 + x * span; s = -90 + y * span
    n = 17
    us, vs, hs = [], [], []
    vals = [[height(s + span * j / (n - 1), w + span * i / (n - 1)) for i in range(n)] for j in range(n)]
    lo = min(min(r) for r in vals); hi = max(max(r) for r in vals) + 1e-3
    for j in range(n):
        for i in range(n):
            us.append(round(32767 * i / (n - 1))); vs.append(round(32767 * j / (n - 1)))
            hs.append(round(32767 * (vals[j][i] - lo) / (hi - lo)))
    out = bytearray(struct.pack('<3d2f4d3d', 0, 0, 0, lo, hi, 0, 0, 0, 0, 0, 0, 0))
    out += struct.pack('<I', len(us))
    for arr in (us, vs, hs):
        prev = 0
        for a in arr:
            out += struct.pack('<H', zz(a - prev) & 0xFFFF); prev = a
    if len(out) % 2: out += b'\0'
    tris = []
    for j in range(n - 1):
        for i in range(n - 1):
            a = j * n + i; b = a + 1; c = a + n; d = c + 1
            tris += [a, b, d, a, d, c]
    order = {}
    for t in tris:
        if t not in order: order[t] = len(order)
    inv = sorted(order, key=lambda k: order[k])
    us = [us[k] for k in inv]; vs = [vs[k] for k in inv]; hs = [hs[k] for k in inv]
    tris = [order[t] for t in tris]
    out = bytearray(struct.pack('<3d2f4d3d', 0, 0, 0, lo, hi, 0, 0, 0, 0, 0, 0, 0))
    out += struct.pack('<I', len(us))
    for arr in (us, vs, hs):
        prev = 0
        for a in arr:
            out += struct.pack('<H', zz(a - prev) & 0xFFFF); prev = a
    if len(out) % 2: out += bytes(1)
    out += struct.pack('<I', len(tris) // 3)
    highest = 0
    for t in tris:
        code = highest - t
        out += struct.pack('<H', code)
        if code == 0: highest += 1
    for _ in range(4): out += struct.pack('<I', 0)
    return bytes(out)
lat, lon = 47.0, -122.0
for z in range(0, 15):
    span = 180.0 / (1 << z)
    x = int((lon + 180) // span); y = int((lat + 90) // span)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            d = os.path.join(root, str(z), str(x + dx)); os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, f'{y + dy}.terrain'), 'wb') as f: f.write(gzip.compress(tile(z, x + dx, y + dy)))
print('ok', height(47.0, -122.0))
