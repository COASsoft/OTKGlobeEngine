import argparse
import collections
import math
import struct
import time
import zlib

import numpy as np
import shapely
from fontTools.ttLib import TTFont
from pyogrio import raw
from scipy.spatial import cKDTree

Q = 1e7
UNIT = 1e-4
PLACE_UNIT = 1e-4
FINE = 10.0
PLACE_CELL = 5.0
COARSE = 30.0
TOLERANCE = {0: 25.0, 1: 25.0, 2: 50.0, 3: 200.0}
COARSE_TOLERANCE = 2000.0
COUNTRY, STATE, COUNTY, CITY, SUBURB = 0, 1, 2, 3, 4
T0 = time.time()


def log(*a):
    print(f'[{time.time() - T0:7.1f}]', *a, flush=True)


def load(path, geometry=True):
    meta, _, wkb, fields = raw.read(path, read_geometry=geometry)
    names = list(meta['fields'])
    attrs = {n: fields[i] for i, n in enumerate(names)}
    return (shapely.from_wkb(wkb) if geometry else None), attrs


def shared_segments(geoms, sides=False):
    parts, pidx = shapely.get_parts(geoms, return_index=True)
    rings, ridx = shapely.get_rings(parts, return_index=True)
    ring_poly = pidx[ridx]
    coords, cidx = shapely.get_coordinates(rings, return_index=True)
    q = np.round(coords * Q).astype(np.int64)
    same = cidx[1:] == cidx[:-1]
    a = q[:-1][same]
    b = q[1:][same]
    poly = ring_poly[cidx[:-1][same]]
    swap = (a[:, 0] > b[:, 0]) | ((a[:, 0] == b[:, 0]) & (a[:, 1] > b[:, 1]))
    lo = np.where(swap[:, None], b, a)
    hi = np.where(swap[:, None], a, b)
    keep = ~np.all(lo == hi, axis=1)
    key = np.concatenate([lo, hi], axis=1)[keep]
    poly = poly[keep]
    swap = swap[keep]
    order = np.lexsort((key[:, 3], key[:, 2], key[:, 1], key[:, 0]))
    key = key[order]
    poly = poly[order]
    swap = swap[order]
    diff = np.any(key[1:] != key[:-1], axis=1)
    start = np.concatenate([[0], np.nonzero(diff)[0] + 1])
    counts = np.diff(np.concatenate([start, [len(key)]]))
    sel = start[counts == 2]
    pa, pb = poly[sel], poly[sel + 1]
    ok = pa != pb
    if not sides:
        return key[sel][ok], pa[ok], pb[ok]
    sa, sb = swap[sel], swap[sel + 1]
    left = np.where(sa == sb, -1, np.where(sa, pb, pa))
    return key[sel][ok], pa[ok], pb[ok], left[ok]


def chains(seg, tag=None):
    if len(seg) == 0:
        return [] if tag is None else ([], [])
    n = len(seg)
    if tag is None:
        ends = np.concatenate([seg[:, :2], seg[:, 2:]])
    else:
        t = np.asarray(tag, dtype=np.int64)[:, None]
        ends = np.concatenate([np.hstack([seg[:, :2], t]), np.hstack([seg[:, 2:], t])])
    nodes, inv = np.unique(ends, axis=0, return_inverse=True)
    inv = inv.ravel()
    u, v = inv[:n], inv[n:]
    lo_node = u.tolist()
    deg = np.bincount(np.concatenate([u, v]), minlength=len(nodes))
    order = np.argsort(np.concatenate([u, v]), kind='stable')
    other = np.concatenate([v, u])[order].tolist()
    edge = np.concatenate([np.arange(n), np.arange(n)])[order].tolist()
    first = np.concatenate([[0], np.cumsum(deg)]).tolist()
    deg = deg.tolist()
    used = [False] * n
    out = []

    def walk(start, e0, nxt):
        path = [start, nxt]
        used[e0] = True
        cur = nxt
        while deg[cur] == 2 and cur != start:
            k = first[cur]
            if not used[edge[k]]:
                e, o = edge[k], other[k]
            elif not used[edge[k + 1]]:
                e, o = edge[k + 1], other[k + 1]
            else:
                break
            used[e] = True
            path.append(o)
            cur = o
        return path

    starts = []
    for s in range(len(nodes)):
        if deg[s] != 2:
            for k in range(first[s], first[s + 1]):
                if not used[edge[k]]:
                    starts.append((edge[k], s == lo_node[edge[k]]))
                    out.append(walk(s, edge[k], other[k]))
    for s in range(len(nodes)):
        for k in range(first[s], first[s + 1]):
            if not used[edge[k]]:
                starts.append((edge[k], s == lo_node[edge[k]]))
                out.append(walk(s, edge[k], other[k]))
    paths = [nodes[np.array(p)][:, :2].astype(np.float64) / Q for p in out]
    return paths if tag is None else (paths, starts)


def importance(pts):
    n = len(pts)
    imp = np.zeros(n)
    imp[0] = imp[-1] = np.inf
    if n <= 2:
        return imp
    lat = np.radians(pts[:, 1].mean())
    x = pts[:, 0] * np.cos(lat) * 111320.0
    y = pts[:, 1] * 111320.0
    if np.hypot(x[0] - x[-1], y[0] - y[-1]) < 1.0:
        k = int(np.argmax(np.hypot(x - x[0], y - y[0])))
        if 0 < k < n - 1:
            imp[k] = np.inf
            stack = [(0, k, np.inf), (k, n - 1, np.inf)]
        else:
            stack = [(0, n - 1, np.inf)]
    else:
        stack = [(0, n - 1, np.inf)]
    while stack:
        i, j, cap = stack.pop()
        if j - i < 2:
            continue
        xs, ys = x[i + 1:j], y[i + 1:j]
        dx, dy = x[j] - x[i], y[j] - y[i]
        length = math.hypot(dx, dy)
        if length > 0:
            d = np.abs(dx * (ys - y[i]) - dy * (xs - x[i])) / length
        else:
            d = np.hypot(xs - x[i], ys - y[i])
        k = int(np.argmax(d))
        dk = float(d[k])
        if dk < 5.0:
            continue
        k += i + 1
        val = min(dk, cap)
        imp[k] = val
        stack.append((i, k, val))
        stack.append((k, j, val))
    return imp


def densify(p, imp, maxdeg):
    d = np.abs(np.diff(p, axis=0)).max(axis=1)
    n = np.maximum(np.ceil(d / maxdeg).astype(np.int64), 1)
    if (n <= 1).all():
        return p, imp
    pts, imps = [p[:1]], [imp[:1]]
    for i in range(len(p) - 1):
        k = int(n[i])
        if k > 1:
            t = (np.arange(1, k) / k)[:, None]
            pts.append(p[i] + (p[i + 1] - p[i]) * t)
            imps.append(np.zeros(k - 1))
        pts.append(p[i + 1:i + 2])
        imps.append(imp[i + 1:i + 2])
    return np.concatenate(pts), np.concatenate(imps)


def boundaries(cgaz):
    g0, a0 = load(f'{cgaz}/geoBoundariesCGAZ_ADM0.shp')
    seg, pa, pb = shared_segments(g0)
    disputed = (a0['shapeType'][pa] == 'DISP') | (a0['shapeType'][pb] == 'DISP')
    out = {0: chains(seg[~disputed]), 1: chains(seg[disputed])}
    log('country chains', len(out[0]), 'disputed chains', len(out[1]))
    g1, a1 = load(f'{cgaz}/geoBoundariesCGAZ_ADM1.shp')
    seg, pa, pb = shared_segments(g1)
    out[2] = chains(seg[a1['shapeGroup'][pa] == a1['shapeGroup'][pb]])
    log('state chains', len(out[2]))
    g2, a2 = load(f'{cgaz}/geoBoundariesCGAZ_ADM2.shp')
    tree = shapely.STRtree(g1)
    ia, ib = tree.query(shapely.point_on_surface(g2), predicate='within')
    member = np.full(len(g2), -1)
    member[ia] = ib
    seg, pa, pb, left = shared_segments(shapely.orient_polygons(g2), True)
    ma, mb = member[pa], member[pb]
    county = (ma == mb) & (ma >= 0) & (a2['shapeGroup'][pa] == a2['shapeGroup'][pb])
    seg, pa, pb, left = seg[county], pa[county], pb[county], left[county]
    pair = np.unique(np.stack([np.minimum(pa, pb), np.maximum(pa, pb)], axis=1), axis=0, return_inverse=True)[1].ravel()
    out[3], starts = chains(seg, pair)
    sides = []
    for e, forward in starts:
        lp = int(left[e])
        rp = int(pb[e]) if lp == int(pa[e]) else int(pa[e])
        sides.append((-1, -1) if lp < 0 else (lp, rp) if forward else (rp, lp))
    log('county chains', len(out[3]), 'without sides', sum(s[0] < 0 for s in sides), 'sides checked', check_sides(g2, out[3], sides))
    lines = []
    for level, cs in out.items():
        for i, c in enumerate(cs):
            lines.append((level, c, importance(c), sides[i] if level == 3 else None))
    log('importance done', len(lines))
    return lines, (g0, a0), (g1, a1), (g2, a2, member)


def check_sides(geoms, paths, sides):
    idx, xs, ys = [], [], []
    for p, (lp, _) in zip(paths, sides):
        if lp < 0:
            continue
        d = np.diff(p, axis=0)
        length = np.hypot(d[:, 0], d[:, 1])
        k = int(np.argmax(length))
        if length[k] <= 0:
            continue
        step = min(1e-5, 0.25 * length[k]) / length[k]
        mid = (p[k] + p[k + 1]) * 0.5
        idx.append(lp)
        xs.append(mid[0] - d[k, 1] * step)
        ys.append(mid[1] + d[k, 0] * step)
    inside = shapely.contains_xy(geoms[np.array(idx)], np.array(xs), np.array(ys))
    return f'{int(inside.sum())}/{len(idx)}'


def zigzag(v):
    v = np.asarray(v, dtype=np.int64)
    return ((v << 1) ^ (v >> 63)).astype(np.uint64)


def varints(values):
    out = bytearray()
    for v in np.asarray(values, dtype=np.uint64).tolist():
        while v >= 0x80:
            out.append((v & 0x7F) | 0x80)
            v >>= 7
        out.append(v)
    return bytes(out)


def varint(v):
    return varints([v])


def planes(values):
    v = np.asarray(values, dtype=np.uint64)
    if len(v) and int(v.max()) >= 1 << 24:
        raise ValueError('delta out of range')
    v = v.astype(np.uint32)
    return (v & 0xFF).astype(np.uint8).tobytes() + ((v >> 8) & 0xFF).astype(np.uint8).tobytes() + ((v >> 16) & 0xFF).astype(np.uint8).tobytes()


def importance_class(imp):
    out = np.full(len(imp), 255, np.uint8)
    finite = np.isfinite(imp)
    k = np.floor(2.0 * np.log2(np.maximum(imp[finite], 25.0) / 25.0))
    out[finite] = np.clip(k, 0, 254).astype(np.uint8)
    return out


def encode_lines(items, with_class, place_of=None):
    place_of = place_of or {}
    head = bytearray(varint(len(items)))
    xs, ys, cl = [], [], []
    px = py = 0
    total = 0
    for level, p, imp, side in items:
        q = np.round(p / UNIT).astype(np.int64)
        head += bytes([level]) + varint(len(q))
        if level == 3:
            lp, rp = side or (-1, -1)
            head += varint(place_of.get(lp, -1) + 1) + varint(place_of.get(rp, -1) + 1)
        xs.append(np.diff(q[:, 0], prepend=px))
        ys.append(np.diff(q[:, 1], prepend=py))
        px, py = int(q[-1, 0]), int(q[-1, 1])
        if with_class:
            cl.append(importance_class(imp))
        total += len(q)
    body = bytes(head) + planes(zigzag(np.concatenate(xs))) + planes(zigzag(np.concatenate(ys)))
    if with_class:
        body += np.concatenate(cl).tobytes()
    return body, total


def grid_runs(lines, size):
    ncols, nrows = int(round(360 / size)), int(round(180 / size))
    cells = collections.defaultdict(list)
    for level, p, imp, side in lines:
        if len(p) < 2:
            continue
        mx = (p[:-1, 0] + p[1:, 0]) * 0.5
        my = (p[:-1, 1] + p[1:, 1]) * 0.5
        c = np.clip(np.floor((mx + 180) / size), 0, ncols - 1).astype(np.int64)
        r = np.clip(np.floor((my + 90) / size), 0, nrows - 1).astype(np.int64)
        keys = r * ncols + c
        split = np.nonzero(np.diff(keys) != 0)[0] + 1
        bounds = np.concatenate([[0], split, [len(keys)]])
        for a, b in zip(bounds[:-1], bounds[1:]):
            cells[int(keys[a])].append((level, p[a:b + 1], imp[a:b + 1], side))
    return cells


def polylabel_point(geom, area_deg):
    parts = shapely.get_parts(geom)
    part = max(parts, key=lambda g: g.area) if len(parts) else geom
    simple = shapely.simplify(part, max(math.sqrt(part.area) * 0.003, 1e-5))
    if simple.is_empty or not simple.is_valid or simple.geom_type != 'Polygon':
        simple = part
    try:
        from shapely.ops import polylabel
        pt = polylabel(simple, max(math.sqrt(simple.area) * 0.01, 1e-5))
        if part.contains(pt):
            return pt.x, pt.y
    except Exception:
        pass
    pt = part.point_on_surface()
    return pt.x, pt.y


def area_km2(geom):
    c = geom.centroid
    return geom.area * 111.32 * 111.32 * max(math.cos(math.radians(c.y)), 0.01)


def size_zoom(lat, size_km, pixels):
    return math.log2(156543.034 * max(math.cos(math.radians(lat)), 0.05) * pixels / max(size_km * 1000.0, 1.0))


def fix_text(s):
    if s is None:
        return None
    s = str(s).strip()
    if any(ch in s for ch in 'ÃÂÅÄ\u0080\u0087\u0089\u009a'):
        try:
            s = s.encode('latin-1').decode('utf-8')
        except (UnicodeEncodeError, UnicodeDecodeError):
            pass
    return s


def wrap(name, limit):
    words = name.split(' ')
    if len(name) <= limit or len(words) < 2:
        return name
    best, score = name, 1e9
    for i in range(1, len(words)):
        a, b = ' '.join(words[:i]), ' '.join(words[i:])
        s = max(len(a), len(b))
        if s < score:
            best, score = a + '\n' + b, s
    if score > limit * 1.6 and len(words) >= 3:
        for i in range(1, len(words) - 1):
            for j in range(i + 1, len(words)):
                parts = [' '.join(words[:i]), ' '.join(words[i:j]), ' '.join(words[j:])]
                s = max(len(x) for x in parts)
                if s < score:
                    best, score = '\n'.join(parts), s
    return best


def read_codes(path):
    out = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            cols = line.rstrip('\n').split('\t')
            if len(cols) >= 2:
                out[cols[0]] = cols[1]
    return out


def read_cities(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            c = line.rstrip('\n').split('\t')
            if len(c) < 15:
                continue
            rows.append((c[1], c[2], float(c[4]), float(c[5]), c[7], c[8], c[10], c[11], int(c[14] or 0)))
    return rows


def places(args, adm0, adm1, adm2, cmap):
    def usable(s):
        return bool(s) and '?' not in s and all(ord(ch) in cmap for ch in s if ch != '\n')

    out = []
    ne0_geom, ne0 = load(f'{args.ne}/ne_10m_admin_0_countries.shp')
    for i in range(len(ne0['NAME'])):
        name = fix_text(ne0['NAME'][i])
        if not name:
            continue
        if '.' in name and ne0['NAME_LONG'][i]:
            name = fix_text(ne0['NAME_LONG'][i])
        name = {'United States of America': 'United States', 'eSwatini': 'Eswatini'}.get(name, name)
        if not usable(name):
            continue
        lon, lat = float(ne0['LABEL_X'][i]), float(ne0['LABEL_Y'][i])
        size = math.sqrt(max(area_km2(ne0_geom[i]), 1.0))
        minz = max(float(ne0['MIN_LABEL'][i]), size_zoom(lat, size, 25.0))
        maxz = max(float(ne0['MAX_LABEL'][i]) + 1.5, minz + 2.0)
        pop = int(max(ne0['POP_EST'][i] or 0, 0))
        out.append((COUNTRY, 0, lat, lon, minz, maxz, pop, wrap(name, 14)))
    log('countries', len(out))

    cities = read_cities(f'{args.geonames}/cities500.txt')
    admin1 = read_codes(f'{args.geonames}/admin1CodesASCII.txt')
    admin2 = read_codes(f'{args.geonames}/admin2Codes.txt')
    lat = np.array([c[2] for c in cities])
    lon = np.array([c[3] for c in cities])
    pts = shapely.points(lon, lat)

    g1, a1 = adm1
    g2, a2, member = adm2
    count1 = collections.Counter(a1['shapeGroup'])
    count2 = collections.Counter(a2['shapeGroup'])

    def votes(geoms, key):
        tree = shapely.STRtree(geoms)
        ia, ib = tree.query(pts, predicate='within')
        v = collections.defaultdict(collections.Counter)
        for p, g in zip(ia.tolist(), ib.tolist()):
            k = key(cities[p])
            if k:
                v[g][k] += 1
        owner = {}
        for g, c in v.items():
            for k, n in c.items():
                if k not in owner or n > v[owner[k]][k]:
                    owner[k] = g
        names = {}
        for g, c in v.items():
            k, n = c.most_common(1)[0]
            if n / sum(c.values()) >= 0.6 and owner.get(k) == g:
                names[g] = k
        return names

    v1 = votes(g1, lambda c: f'{c[5]}.{c[6]}' if c[6] else None)
    v2 = votes(g2, lambda c: f'{c[5]}.{c[6]}.{c[7]}' if c[7] else None)
    log('votes', len(v1), len(v2))

    _, ne1 = load(f'{args.ne}/ne_10m_admin_1_states_provinces.shp', geometry=False)
    ne1_geom, _ = load(f'{args.ne}/ne_10m_admin_1_states_provinces.shp')
    ne1_tree = shapely.STRtree(ne1_geom)
    simple1 = shapely.simplify(g1, 0.01)

    areas1 = np.array([area_km2(g) for g in g1])
    states = 0
    for i, g in enumerate(g1):
        group = a1['shapeGroup'][i]
        if count1[group] < 2 or a1['shapeType'][i] == 'DISP':
            continue
        name = None
        if i in v1 and admin1.get(v1[i]):
            name = admin1[v1[i]]
        label = None
        cand = ne1_tree.query(simple1[i], predicate='intersects')
        best, best_iou = None, 0.0
        for j in cand.tolist():
            try:
                inter = shapely.intersection(simple1[i], ne1_geom[j]).area
                union = simple1[i].area + ne1_geom[j].area - inter
            except shapely.errors.GEOSException:
                continue
            iou = inter / union if union > 0 else 0
            if iou > best_iou:
                best, best_iou = j, iou
        if best is not None and best_iou >= 0.5:
            if not name:
                name = fix_text(ne1['name_en'][best] or ne1['name'][best])
            plat, plon = ne1['latitude'][best], ne1['longitude'][best]
            if plat is not None and plon is not None and g.contains(shapely.Point(float(plon), float(plat))):
                label = (float(plon), float(plat))
        if not usable(name):
            name = fix_text(a1['shapeName'][i])
        if not usable(name):
            continue
        if label is None:
            label = polylabel_point(g, g.area)
        size = math.sqrt(areas1[i])
        minz = max(size_zoom(label[1], size, 100.0), 3.0)
        maxz = size_zoom(label[1], size, 1800.0)
        if maxz <= minz:
            continue
        out.append((STATE, 0, label[1], label[0], minz, maxz, int(areas1[i]), wrap(name.upper(), 9)))
        states += 1
    log('states', states)

    counties = 0
    county_items = {}
    for i, g in enumerate(g2):
        group = a2['shapeGroup'][i]
        if count2[group] <= count1[group] or member[i] < 0:
            continue
        area = area_km2(g)
        if area > 0.9 * areas1[member[i]]:
            continue
        name = None
        if i in v2 and admin2.get(v2[i]):
            name = admin2[v2[i]]
        if not usable(name):
            name = fix_text(a2['shapeName'][i])
        if not usable(name) or name.isdigit():
            continue
        lx, ly = polylabel_point(g, g.area)
        size = math.sqrt(area)
        minz = max(size_zoom(ly, size, 80.0), 8.0)
        maxz = size_zoom(ly, size, 1500.0)
        if maxz <= minz:
            continue
        county_items[i] = len(out)
        out.append((COUNTY, 0, ly, lx, minz, maxz, int(area), wrap(name, 16)))
        counties += 1
    log('counties', counties)

    keep_codes = {'PPL', 'PPLA', 'PPLA2', 'PPLA3', 'PPLA4', 'PPLA5', 'PPLC', 'PPLG', 'PPLS', 'PPLL', 'PPLF', 'PPLR', 'STLMT', 'PPLX'}
    sel = [i for i, c in enumerate(cities) if c[4] in keep_codes]
    lat_s = lat[sel]
    lon_s = lon[sel]
    pop = np.array([cities[i][8] for i in sel], dtype=np.float64)
    code = [cities[i][4] for i in sel]
    mult = np.array([3.0 if c == 'PPLC' else 1.3 if c == 'PPLA' else 1.1 if c == 'PPLA2' else 0.3 if c == 'PPLX' else 1.0 for c in code])
    score = np.maximum(pop, 1.0) * mult
    order = np.argsort(-score, kind='stable')
    rank = np.empty(len(sel), np.int64)
    rank[order] = np.arange(len(sel))
    phi, lam = np.radians(lat_s), np.radians(lon_s)
    xyz = np.stack([np.cos(phi) * np.cos(lam), np.cos(phi) * np.sin(lam), np.sin(phi)], axis=1)
    tree = cKDTree(xyz)
    iso = np.full(len(sel), np.inf)
    pending = np.arange(len(sel))
    for k in (16, 128, 1024):
        if len(pending) == 0:
            break
        d, j = tree.query(xyz[pending], k=min(k, len(sel)))
        higher = rank[j] < rank[pending][:, None]
        found = higher.any(axis=1)
        first = np.argmax(higher, axis=1)
        iso[pending[found]] = d[found, first[found]]
        pending = pending[~found]
    for p in pending.tolist():
        better = order[:rank[p]]
        if len(better):
            iso[p] = np.min(np.linalg.norm(xyz[better] - xyz[p], axis=1))
    iso_m = 2.0 * np.arcsin(np.minimum(iso / 2.0, 1.0)) * 6371008.8
    floors = [(5e6, 3.0), (2e6, 3.5), (1e6, 4.0), (5e5, 4.5), (2e5, 5.0), (1e5, 5.5), (5e4, 6.0), (2e4, 7.0), (1e4, 7.5), (5e3, 8.5), (2e3, 9.5), (1e3, 10.5)]
    _, nepp = load(f'{args.ne}/ne_10m_populated_places.shp', geometry=False)
    ne_names = {}
    ne_tree = cKDTree(xyz)
    for j in range(len(nepp['NAME'])):
        nlat, nlon = float(nepp['LATITUDE'][j]), float(nepp['LONGITUDE'][j])
        p3 = np.array([math.cos(math.radians(nlat)) * math.cos(math.radians(nlon)), math.cos(math.radians(nlat)) * math.sin(math.radians(nlon)), math.sin(math.radians(nlat))])
        hits = ne_tree.query_ball_point(p3, 15.0 / 6371.0)
        if not hits:
            continue
        best = max(hits, key=lambda h: pop[h])
        ne_names[best] = fix_text(nepp['NAME'][j])
    towns = 0
    for n, i in enumerate(sel):
        c = cities[i]
        name = c[0] if usable(c[0]) else c[1]
        short = ne_names.get(n)
        if short and usable(short) and name.startswith(short + ' ') and len(short) >= 4:
            name = short
        if not usable(name):
            continue
        p = c[8]
        floor = 11.5
        for threshold, z in floors:
            if p >= threshold:
                floor = z
                break
        flags = 0
        if c[4] == 'PPLC':
            floor = min(floor, 3.0)
            flags |= 1
        elif c[4] == 'PPLA':
            floor -= 0.5
            flags |= 2
        kind = CITY
        if c[4] == 'PPLX':
            kind = SUBURB
            floor = max(floor, 12.0)
        if iso_m[n] < 50.0:
            continue
        isoz = math.log2(156543.034 * max(math.cos(math.radians(c[2])), 0.05) * 55.0 / iso_m[n]) if np.isfinite(iso_m[n]) else 0.0
        minz = max(floor, isoz)
        if minz > 15.5:
            continue
        out.append((kind, flags, c[2], c[3], minz, 99.0, p, name))
        towns += 1
    log('towns', towns)
    return out, county_items


def morton(x, y):
    v = 0
    for b in range(16):
        v |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1)
    return v


def encode_places(items):
    ncols, nrows = int(360 / PLACE_CELL), int(180 / PLACE_CELL)

    def cell(lat, lon):
        c = min(max(int(math.floor((lon + 180) / PLACE_CELL)), 0), ncols - 1)
        r = min(max(int(math.floor((lat + 90) / PLACE_CELL)), 0), nrows - 1)
        return r * ncols + c

    def key(t):
        qx = int((t[3] + 180) / 0.01) & 0xFFFF
        qy = int((t[2] + 90) / 0.01) & 0xFFFF
        return (cell(t[2], t[3]), min(max(int(round(t[4] * 10)), 0), 250) // 10, morton(qx, qy))

    order = sorted(range(len(items)), key=lambda k: key(items[k]))
    rank = [0] * len(items)
    for final, k in enumerate(order):
        rank[k] = final
    items = [items[k] for k in order]
    starts = [0] * (ncols * nrows + 1)
    for t in items:
        starts[cell(t[2], t[3]) + 1] += 1
    for i in range(1, len(starts)):
        starts[i] += starts[i - 1]
    kinds = bytes(t[0] for t in items)
    flags = bytes(t[1] for t in items)
    minz = bytes(min(max(int(round(t[4] * 10)), 0), 250) for t in items)
    maxz = bytes(255 if t[5] >= 25 else min(max(int(round(t[5] * 10)), 0), 250) for t in items)
    qlat = np.round(np.array([t[2] for t in items]) / PLACE_UNIT).astype(np.int64)
    qlon = np.round(np.array([t[3] for t in items]) / PLACE_UNIT).astype(np.int64)
    lat_stream = varints(zigzag(np.diff(qlat, prepend=0)))
    lon_stream = varints(zigzag(np.diff(qlon, prepend=0)))
    pops = bytes(min(int(round(math.log2(max(int(t[6]), 1)) * 8)), 255) for t in items)
    names = bytearray()
    for t in items:
        b = t[7].encode('utf-8')
        names += varint(len(b)) + b
    body = struct.pack('<I', len(items)) + struct.pack(f'<{len(starts)}I', *starts)
    for part in (kinds, flags, minz, maxz, lat_stream, lon_stream, pops, bytes(names)):
        body += struct.pack('<I', len(part)) + part
    return body, rank


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cgaz', required=True)
    ap.add_argument('--ne', required=True)
    ap.add_argument('--geonames', required=True)
    ap.add_argument('--font', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    cmap = set(TTFont(args.font).getBestCmap().keys())

    lines, adm0, adm1, adm2 = boundaries(args.cgaz)
    fine, coarse = [], []
    for level, p, imp, side in lines:
        keep = imp >= TOLERANCE[level]
        fp, fi = densify(p[keep], imp[keep], 0.5)
        if len(fp) >= 2:
            fine.append((level, fp, fi, side))
        if level < 3:
            keep = imp >= COARSE_TOLERANCE
            cp, ci = densify(p[keep], imp[keep], 2.0)
            if len(cp) >= 2:
                coarse.append((level, cp, ci, side))
    log('fine vertices', sum(len(t[1]) for t in fine), 'coarse vertices', sum(len(t[1]) for t in coarse))

    blocks = []
    items, county_items = places(args, adm0, adm1, adm2, cmap)
    body, rank = encode_places(items)
    blocks.append((1, 0, body))
    place_of = {poly: rank[item] for poly, item in county_items.items()}
    for key, items in sorted(grid_runs(coarse, COARSE).items()):
        body, _ = encode_lines(items, True)
        blocks.append((2, key, body))
    for key, items in sorted(grid_runs(fine, FINE).items()):
        body, _ = encode_lines(items, False, place_of)
        blocks.append((3, key, body))

    header = b'OTKL' + struct.pack('<II', 2, len(blocks))
    table_size = len(blocks) * 20
    offset = len(header) + table_size
    table, data = bytearray(), bytearray()
    sizes = collections.Counter()
    for kind, key, body in blocks:
        comp = zlib.compress(body, 9)
        table += struct.pack('<IIIII', kind, key, offset + len(data), len(comp), len(body))
        data += comp
        sizes[kind] += len(comp)
    with open(args.out, 'wb') as f:
        f.write(header + table + data)
    log('blocks', len(blocks), 'places', sizes[1], 'coarse', sizes[2], 'fine', sizes[3], 'total', len(header) + len(table) + len(data))


if __name__ == '__main__':
    main()
