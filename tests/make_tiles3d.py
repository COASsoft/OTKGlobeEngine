import argparse, json, math, os, struct, subprocess, zipfile, zlib

LAT, LON, H = 47.04, -122.90, 400.0
A = 6378137.0
E2 = (1 / 298.257223563) * (2 - 1 / 298.257223563)


def ecef(lat, lon, h):
    p, l = math.radians(lat), math.radians(lon)
    n = A / math.sqrt(1 - E2 * math.sin(p) ** 2)
    return [(n + h) * math.cos(p) * math.cos(l), (n + h) * math.cos(p) * math.sin(l), (n * (1 - E2) + h) * math.sin(p)]


def enu_axes(lat, lon):
    p, l = math.radians(lat), math.radians(lon)
    east = [-math.sin(l), math.cos(l), 0]
    north = [-math.sin(p) * math.cos(l), -math.sin(p) * math.sin(l), math.cos(p)]
    up = [math.cos(p) * math.cos(l), math.cos(p) * math.sin(l), math.sin(p)]
    return east, north, up


EAST, NORTH, UP = enu_axes(LAT, LON)
ORIGIN = ecef(LAT, LON, H)
TRANSFORM = EAST + [0] + NORTH + [0] + UP + [0] + ORIGIN + [1]


def to_local(p):
    d = [p[i] - ORIGIN[i] for i in range(3)]
    e, n, u = (sum(d[i] * a[i] for i in range(3)) for a in (EAST, NORTH, UP))
    return [e, u, -n]


def pad(b, n=8, fill=b' '):
    return b + fill * ((n - len(b) % n) % n)


class Gltf:
    def __init__(self):
        self.doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [],
                    'accessors': [], 'bufferViews': [], 'buffers': []}
        self.bin = bytearray()
        self.used = set()
        self.required = set()

    def view(self, data):
        while len(self.bin) % 4:
            self.bin += b'\0'
        self.doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)})
        self.bin += data
        return len(self.doc['bufferViews']) - 1

    def accessor(self, view, ctype, count, kind, values=None, extra=None):
        a = {'componentType': ctype, 'count': count, 'type': kind}
        if view is not None:
            a['bufferView'] = view
        if values is not None:
            a['min'] = [min(v[i] for v in values) for i in range(len(values[0]))]
            a['max'] = [max(v[i] for v in values) for i in range(len(values[0]))]
        if extra:
            a.update(extra)
        self.doc['accessors'].append(a)
        return len(self.doc['accessors']) - 1

    def material(self, color, texture=None):
        m = {'pbrMetallicRoughness': {'baseColorFactor': color, 'metallicFactor': 0, 'roughnessFactor': 1}, 'doubleSided': True}
        if texture is not None:
            m['pbrMetallicRoughness']['baseColorTexture'] = {'index': texture}
        self.doc['materials'].append(m)
        return len(self.doc['materials']) - 1

    def mesh(self, positions, indices, material, uvs=None, mode=4):
        attrs = {'POSITION': self.accessor(self.view(b''.join(struct.pack('<3f', *p) for p in positions)), 5126, len(positions), 'VEC3', positions)}
        if uvs:
            attrs['TEXCOORD_0'] = self.accessor(self.view(b''.join(struct.pack('<2f', *t) for t in uvs)), 5126, len(uvs), 'VEC2')
        prim = {'attributes': attrs, 'material': material, 'mode': mode}
        if indices is not None:
            prim['indices'] = self.accessor(self.view(b''.join(struct.pack('<I', i) for i in indices)), 5125, len(indices), 'SCALAR')
        self.doc['meshes'].append({'primitives': [prim]})
        return len(self.doc['meshes']) - 1

    def node(self, mesh, **kw):
        n = {'mesh': mesh}
        n.update(kw)
        self.doc['nodes'].append(n)
        self.doc['scenes'][0]['nodes'].append(len(self.doc['nodes']) - 1)
        return len(self.doc['nodes']) - 1

    def glb(self):
        doc = dict(self.doc)
        doc['buffers'] = [{'byteLength': len(self.bin)}]
        if self.used:
            doc['extensionsUsed'] = sorted(self.used)
        if self.required:
            doc['extensionsRequired'] = sorted(self.required)
        doc = {k: v for k, v in doc.items() if v != []}
        js = pad(json.dumps(doc).encode(), 4)
        binary = pad(bytes(self.bin), 4, b'\0')
        body = struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(binary), b'BIN\0') + binary
        return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


def quad(x0, n0, x1, n1, up=0.0):
    pts = [[x0, up, -n0], [x1, up, -n0], [x1, up, -n1], [x0, up, -n1]]
    return pts, [0, 1, 2, 0, 2, 3], [[0, 1], [1, 1], [1, 0], [0, 0]]


def quad_glb(x0, n0, x1, n1, color):
    g = Gltf()
    pts, idx, _ = quad(x0, n0, x1, n1)
    g.node(g.mesh(pts, idx, g.material(color)))
    return g.glb()


def box(size):
    s = size / 2
    pts, idx = [], []
    for axis in range(3):
        for sign in (-1, 1):
            base = len(pts)
            u, v = [(1, 2), (2, 0), (0, 1)][axis]
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = [0, 0, 0]
                p[axis] = sign * s
                p[u] = a * s
                p[v] = b * s
                p[1] += s
                pts.append(p)
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pts, idx


def table(header_size, ft_json, ft_bin=b''):
    js = json.dumps(ft_json).encode()
    js += b' ' * ((8 - (header_size + len(js)) % 8) % 8)
    return js, pad(ft_bin, 8, b'\0')


def b3dm(glb):
    js, fb = table(28, {'BATCH_LENGTH': 0})
    body = js + fb + pad(glb, 8, b'\0')
    return struct.pack('<4s6I', b'b3dm', 1, 28 + len(body), len(js), len(fb), 0, 0) + body


def i3dm(glb, positions, scales):
    n = len(positions)
    fb = b''.join(struct.pack('<3f', *p) for p in positions) + b''.join(struct.pack('<f', s) for s in scales)
    js, fb = table(32, {'INSTANCES_LENGTH': n, 'POSITION': {'byteOffset': 0}, 'SCALE': {'byteOffset': 12 * n}, 'EAST_NORTH_UP': True}, fb)
    body = js + fb + pad(glb, 8, b'\0')
    return struct.pack('<4s7I', b'i3dm', 1, 32 + len(body), len(js), len(fb), 0, 0, 1) + body


def pnts(positions, colors, draco=None):
    n = len(positions)
    if draco is None:
        fb = b''.join(struct.pack('<3f', *p) for p in positions) + b''.join(bytes(c) for c in colors)
        ft = {'POINTS_LENGTH': n, 'POSITION': {'byteOffset': 0}, 'RGB': {'byteOffset': 12 * n}}
    else:
        data, props = draco
        fb = data
        ft = {'POINTS_LENGTH': n, 'POSITION': {'byteOffset': 0}, 'RGB': {'byteOffset': 0},
              'extensions': {'3DTILES_draco_point_compression': {'properties': props, 'byteOffset': 0, 'byteLength': len(data)}}}
    js, fb = table(28, ft, fb)
    body = js + fb
    return struct.pack('<4s6I', b'pnts', 1, 28 + len(body), len(js), len(fb), 0, 0) + body


def cmpt(tiles):
    body = b''.join(tiles)
    return struct.pack('<4s3I', b'cmpt', 1, 16 + len(body), len(tiles)) + body


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def tileset(path, root):
    write(os.path.join(path, 'tileset.json'), json.dumps({'asset': {'version': '1.1'}, 'geometricError': 4000, 'root': root}).encode())


def sphere(r, x=0, y=0, z=0):
    return {'sphere': [x, y, z, r]}


def replace_missing(out):
    S = 1000.0
    write(os.path.join(out, 'root.glb'), quad_glb(-S, -S, S, S, [1, 0, 0, 1]))
    kids = []
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        x0, y0, x1, y1 = min(0, sx * S), min(0, sy * S), max(0, sx * S), max(0, sy * S)
        if i != 3:
            write(os.path.join(out, f'q{i}.b3dm'), b3dm(quad_glb(x0, y0, x1, y1, [0, 1, 0, 1])))
        kids.append({'boundingVolume': sphere(S * 0.75, (x0 + x1) / 2, (y0 + y1) / 2), 'geometricError': 0, 'content': {'uri': f'q{i}.b3dm'}})
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(S * 1.5), 'geometricError': 400, 'refine': 'REPLACE',
                  'content': {'uri': 'root.glb'}, 'children': kids})


def grid_positions(count, spacing):
    half = (count - 1) / 2
    return [[(i - half) * spacing, (j - half) * spacing, 0.0] for j in range(count) for i in range(count)]


def instanced(out):
    g = Gltf()
    pts, idx = box(120)
    g.node(g.mesh(pts, idx, g.material([0.1, 0.3, 1, 1])))
    positions = grid_positions(4, 400)
    write(os.path.join(out, 'boxes.i3dm'), i3dm(g.glb(), positions, [1.0 + 0.25 * (i % 4) for i in range(len(positions))]))
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(1500), 'geometricError': 0, 'content': {'uri': 'boxes.i3dm'}})


def gpu_instancing(out):
    g = Gltf()
    pts, idx = box(120)
    mesh = g.mesh(pts, idx, g.material([1, 0, 1, 1]))
    positions = [[p[0], 0.0, -p[1]] for p in grid_positions(4, 400)]
    acc = g.accessor(g.view(b''.join(struct.pack('<3f', *p) for p in positions)), 5126, len(positions), 'VEC3')
    g.node(mesh, extensions={'EXT_mesh_gpu_instancing': {'attributes': {'TRANSLATION': acc}}})
    g.used.add('EXT_mesh_gpu_instancing')
    write(os.path.join(out, 'boxes.glb'), g.glb())
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(1500), 'geometricError': 0, 'content': {'uri': 'boxes.glb'}})


def point_grid(count, spacing, color_fn):
    positions = grid_positions(count, spacing)
    colors = [color_fn(i % count / (count - 1), i // count / (count - 1)) for i in range(len(positions))]
    return positions, colors


def points(out):
    positions, colors = point_grid(100, 20, lambda u, v: (255, int(200 * u), int(60 * v)))
    write(os.path.join(out, 'grid.pnts'), pnts(positions, colors))
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(1500), 'geometricError': 0, 'content': {'uri': 'grid.pnts'}})


def composite(out):
    positions, colors = point_grid(20, 75, lambda u, v: (0, 255, 255))
    positions = [[p[0], p[1], 30.0] for p in positions]
    write(os.path.join(out, 'mixed.cmpt'), cmpt([b3dm(quad_glb(-800, -800, 800, 800, [0, 0.6, 0, 1])), pnts(positions, colors)]))
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(1500), 'geometricError': 0, 'content': {'uri': 'mixed.cmpt'}})


def subtree(tile_bits, content_bits, child_bits):
    buf = bytearray()
    views = []
    doc = {}
    for key, bits in (('tileAvailability', tile_bits), ('contentAvailability', content_bits), ('childSubtreeAvailability', child_bits)):
        if isinstance(bits, int):
            entry = {'constant': bits}
        else:
            data = bytearray((len(bits) + 7) // 8)
            for i, b in enumerate(bits):
                if b:
                    data[i // 8] |= 1 << (i % 8)
            while len(buf) % 8:
                buf.append(0)
            views.append({'buffer': 0, 'byteOffset': len(buf), 'byteLength': len(data)})
            buf += data
            entry = {'bitstream': len(views) - 1}
        doc[key] = [entry] if key == 'contentAvailability' else entry
    if views:
        doc['buffers'] = [{'byteLength': len(buf)}]
        doc['bufferViews'] = views
    js = pad(json.dumps(doc).encode(), 8)
    binary = pad(bytes(buf), 8, b'\0')
    return struct.pack('<4sIQQ', b'subt', 1, len(js), len(binary)) + js + binary


def implicit(out):
    dlat, dlon = 0.01, 0.015
    w, s, e, n = LON - dlon, LAT - dlat, LON + dlon, LAT + dlat
    levels, colors = 4, [[1, 0, 0, 1], [0, 1, 0, 1], [0, 0, 1, 1], [1, 1, 1, 1]]
    for level in range(levels):
        div = 2 ** level
        for x in range(div):
            for y in range(div):
                lw, le = w + (e - w) * x / div, w + (e - w) * (x + 1) / div
                ls, ln = s + (n - s) * y / div, s + (n - s) * (y + 1) / div
                corners = [to_local(ecef(la, lo, H)) for la, lo in ((ls, lw), (ls, le), (ln, le), (ln, lw))]
                g = Gltf()
                g.node(g.mesh(corners, [0, 1, 2, 0, 2, 3], g.material(colors[level])))
                write(os.path.join(out, 'content', str(level), str(x), f'{y}.glb'), g.glb())
    write(os.path.join(out, 'subtrees', '0', '0', '0.subtree'), subtree(1, 1, [1] * 16))
    for x in range(4):
        for y in range(4):
            leaf = (x + y) % 2 == 1
            tiles = [1, 0, 0, 0, 0] if leaf else [1, 1, 1, 1, 1]
            write(os.path.join(out, 'subtrees', '2', str(x), f'{y}.subtree'), subtree(tiles, tiles, 0))
    region = [math.radians(w), math.radians(s), math.radians(e), math.radians(n), H - 50, H + 50]
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': {'region': region}, 'geometricError': 400, 'refine': 'REPLACE',
                  'content': {'uri': 'content/{level}/{x}/{y}.glb'},
                  'implicitTiling': {'subdivisionScheme': 'QUADTREE', 'subtreeLevels': 2, 'availableLevels': levels,
                                     'subtrees': {'uri': 'subtrees/{level}/{x}/{y}.subtree'}}})


def draco_mesh(out, encoder):
    obj = os.path.join(out, 'grid.obj')
    os.makedirs(out, exist_ok=True)
    N, S = 20, 1600.0
    with open(obj, 'w') as f:
        for j in range(N + 1):
            for i in range(N + 1):
                x, y = -S / 2 + S * i / N, -S / 2 + S * j / N
                f.write(f'v {x} {20 * math.sin(i / 3.0)} {-y}\n')
        for j in range(N):
            for i in range(N):
                a = j * (N + 1) + i + 1
                f.write(f'f {a} {a + 1} {a + N + 2}\nf {a} {a + N + 2} {a + N + 1}\n')
    drc = os.path.join(out, 'grid.drc')
    subprocess.run([encoder, '-i', obj, '-o', drc, '-qp', '16'], check=True, capture_output=True)
    data = open(drc, 'rb').read()
    os.remove(drc)
    g = Gltf()
    view = g.view(data)
    vcount = (N + 1) ** 2
    pos = g.accessor(None, 5126, vcount, 'VEC3', None, {'min': [-S / 2, -20, -S / 2], 'max': [S / 2, 20, S / 2]})
    ind = g.accessor(None, 5125, N * N * 6, 'SCALAR')
    prim = {'attributes': {'POSITION': pos}, 'indices': ind, 'material': g.material([1, 0.5, 0, 1]), 'mode': 4,
            'extensions': {'KHR_draco_mesh_compression': {'bufferView': view, 'attributes': {'POSITION': 0}}}}
    g.doc['meshes'].append({'primitives': [prim]})
    g.node(0)
    g.used.add('KHR_draco_mesh_compression')
    g.required.add('KHR_draco_mesh_compression')
    write(os.path.join(out, 'grid.glb'), g.glb())
    os.remove(obj)
    ply = os.path.join(out, 'points.ply')
    positions, colors = point_grid(80, 20, lambda u, v: (40, int(255 * u), 255))
    with open(ply, 'w') as f:
        f.write(f'ply\nformat ascii 1.0\nelement vertex {len(positions)}\nproperty float x\nproperty float y\nproperty float z\n'
                'property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n')
        for p, c in zip(positions, colors):
            f.write(f'{p[0]} {p[1]} {p[2] + 40} {c[0]} {c[1]} {c[2]}\n')
    pdrc = os.path.join(out, 'points.drc')
    subprocess.run([encoder, '-point_cloud', '-i', ply, '-o', pdrc, '-qp', '16', '-qg', '8'], check=True, capture_output=True)
    os.remove(ply)
    write(os.path.join(out, 'points.pnts'), pnts(positions, colors, (open(pdrc, 'rb').read(), {'POSITION': 0, 'RGB': 1})))
    os.remove(pdrc)
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(1500), 'geometricError': 400, 'refine': 'ADD', 'content': {'uri': 'grid.glb'},
                  'children': [{'boundingVolume': sphere(1500), 'geometricError': 0, 'content': {'uri': 'points.pnts'}}]})


def ktx2_texture(out, basisu, mode):
    os.makedirs(out, exist_ok=True)
    W = 64
    png = os.path.join(out, 'checker.png')
    rows = b''.join(b'\0' + b''.join(bytes((255, 255, 255) if (x // 8 + y // 8) % 2 else (200, 30, 30)) for x in range(W)) for y in range(W))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(png, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, W, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    ktx = os.path.join(out, 'checker.ktx2')
    args = [basisu, '-uastc' if mode == 'uastc' else '-etc1s', '-mipmap', '-output_file', ktx, png]
    subprocess.run(args, check=True, capture_output=True, cwd=out)
    os.remove(png)
    g = Gltf()
    view = g.view(open(ktx, 'rb').read())
    os.remove(ktx)
    g.doc['images'] = [{'bufferView': view, 'mimeType': 'image/ktx2'}]
    g.doc['textures'] = [{'extensions': {'KHR_texture_basisu': {'source': 0}}}]
    g.used.add('KHR_texture_basisu')
    g.required.add('KHR_texture_basisu')
    pts, idx, uvs = quad(-800, -800, 800, 800)
    g.node(g.mesh(pts, idx, g.material([1, 1, 1, 1], 0), uvs))
    write(os.path.join(out, 'quad.glb'), g.glb())
    tileset(out, {'transform': TRANSFORM, 'boundingVolume': sphere(1500), 'geometricError': 0, 'content': {'uri': 'quad.glb'}})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--draco-encoder')
    ap.add_argument('--basisu')
    args = ap.parse_args()
    replace_missing(os.path.join(args.out, 'replace_missing'))
    instanced(os.path.join(args.out, 'i3dm'))
    gpu_instancing(os.path.join(args.out, 'gpu_instancing'))
    points(os.path.join(args.out, 'pnts'))
    composite(os.path.join(args.out, 'cmpt'))
    implicit(os.path.join(args.out, 'implicit'))
    if args.draco_encoder:
        draco_mesh(os.path.join(args.out, 'draco'), args.draco_encoder)
    if args.basisu:
        ktx2_texture(os.path.join(args.out, 'ktx2_etc1s'), args.basisu, 'etc1s')
        ktx2_texture(os.path.join(args.out, 'ktx2_uastc'), args.basisu, 'uastc')
    for name in ('implicit',):
        src = os.path.join(args.out, name)
        with zipfile.ZipFile(src + '.3tz', 'w', zipfile.ZIP_STORED) as z:
            for root, _, files in os.walk(src):
                for f in files:
                    full = os.path.join(root, f)
                    z.write(full, os.path.relpath(full, src).replace(os.sep, '/'))


if __name__ == '__main__':
    main()
