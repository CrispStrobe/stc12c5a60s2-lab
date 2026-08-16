#!/usr/bin/env python3
"""Geometric netlist extraction from KiCad 7 .kicad_sch — for transcribing
MIT-licensed reference designs (PainfulDiodes Z80) into bench netlists.

Everything needed is inside the sheet file: lib_symbols carry per-symbol pin
geometry; symbol instances carry placement (at x y rot, mirror); wires are
point pairs; labels name nets at points. Connectivity = union-find over
coincident points. Rotation/mirror follow KiCad's conventions (Y down,
rotation CCW in schematic space, mirror before rotate).
Usage: kicad-netlist.py <sheet.kicad_sch>
"""
import sys, re, math
from collections import defaultdict

def tokenize(s):
    for m in re.finditer(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', s):
        yield m.group(0)

def parse(tokens):
    t = next(tokens)
    if t != '(': return t[1:-1] if t.startswith('"') else t
    out = []
    for t in tokens:
        if t == ')': return out
        if t == '(':
            out.append(parse_rest(tokens))
        else:
            out.append(t[1:-1] if t.startswith('"') else t)
    return out

def parse_rest(tokens):
    out = []
    for t in tokens:
        if t == ')': return out
        if t == '(':
            out.append(parse_rest(tokens))
        else:
            out.append(t[1:-1] if t.startswith('"') else t)
    return out

def walk(node, name):
    if isinstance(node, list):
        if node and node[0] == name: yield node
        for ch in node:
            yield from walk(ch, name)

def main(path):
    sexp = parse(tokenize(open(path).read()))
    # 1. Library pin geometry, PER UNIT: lib_id -> unit -> [(num, name, x, y)]
    # KiCad lib symbols nest child symbols named "<name>_<unit>_<bodystyle>";
    # unit 0 = common pins, and a multi-unit part (a hex inverter) has one
    # child per gate. Collecting ALL children's pins for every instance is
    # how the first extraction placed six gates' pins on each gate and
    # over-merged the control nets.
    libpins = {}
    lib_section = next(walk(sexp, 'lib_symbols'), [])
    for lib in (n for n in lib_section if isinstance(n, list) and n and n[0] == 'symbol'):
        name = lib[1]
        childbase = name.split(':')[-1]
        units = {}
        for child in (n for n in lib if isinstance(n, list) and n and n[0] == 'symbol'):
            cname = child[1]
            m = re.match(re.escape(childbase) + r'_(\d+)_(\d+)$', cname)
            if not m: continue
            unit, style = int(m.group(1)), int(m.group(2))
            pins = []
            for pin in walk(child, 'pin'):
                at = next(walk(pin, 'at'), None)
                num = next(walk(pin, 'number'), None)
                nam = next(walk(pin, 'name'), None)
                if at and num:
                    pins.append((num[1], nam[1] if nam else '', float(at[1]), float(at[2])))
            if pins:
                # Keep the LOWEST body style per unit (style 0/1 = normal
                # body; higher = DeMorgan alternates). The 74LS04's pin
                # children are style 0 — a !=1 filter dropped every gate.
                cur = units.get(unit)
                if cur is None or style < cur[0]:
                    units[unit] = (style, pins)
        libpins[name] = {u: p for u, (st, p) in units.items()}
    # 2. Points registry with union-find.
    parent = {}
    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    def union(a, b): parent[find(a)] = find(b)
    def key(x, y): return (round(x * 100), round(y * 100))
    pin_at = {}   # point -> [(ref, pinnum, pinname)]
    ref_lib = {}  # refdes -> first lib base seen (collision detection)
    label_at = {} # point -> label
    # Instances: (symbol (lib_id "X") (at x y rot) [mirror] … (property "Reference" "U1"))
    for inst in walk(sexp, 'symbol'):
        libid = next((n[1] for n in walk(inst, 'lib_id') if len(n) > 1), None)
        if not libid: continue
        at = next((n for n in walk(inst, 'at') if len(n) >= 3), None)
        if not at: continue
        ref = next((n[2] for n in walk(inst, 'property') if len(n) > 2 and n[1] == 'Reference'), '?')
        if ref.startswith('#'): continue
        # The upstream schematic REUSES refdes across different parts (its
        # U1 is both the Z80 and a 74LS32 package). Qualify collisions so
        # the netlist tells the truth instead of merging two chips.
        base_l = libid.split(':')[-1]
        prev = ref_lib.setdefault(ref, base_l)
        if prev != base_l:
            ref = f"{ref}@{base_l}" 
        ox, oy, rot = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
        mir = next((n[1] for n in walk(inst, 'mirror') if len(n) > 1), None)
        base = libid.split(':')[-1]
        units = libpins.get(libid) or libpins.get(base) or next((v for k, v in libpins.items() if k.endswith(base)), {})
        inst_unit = int(next((n[1] for n in walk(inst, 'unit') if len(n) > 1), 1))
        pins = list(units.get(0, [])) + list(units.get(inst_unit, []))
        seen_coords = {}
        for num, nam, px, py in pins:
            # Transform order (KiCad 7): mirror in LIB space, rotate CCW in
            # LIB space, THEN flip Y into sheet space (Y grows down). The
            # first version composed the flip before the rotation, which is
            # rot(-theta) -- correct at 0/180, wrong at 90/270, and the
            # cause of the over-merged nets (rotated gates' pins landed on
            # foreign junctions).
            x, y = px, py
            if mir == 'y': x = -x
            if mir == 'x': y = -y
            r = math.radians(rot)
            lx = x * math.cos(r) - y * math.sin(r)
            ly = x * math.sin(r) + y * math.cos(r)
            k = key(ox + lx, oy - ly)
            if k in seen_coords:
                print(f"WARN: {ref} pins {seen_coords[k]} and {num} coincide -- transform bug", file=sys.stderr)
            seen_coords[k] = num
            pin_at.setdefault(k, []).append((ref, num, nam))
    for w in walk(sexp, 'wire'):
        pts = [key(float(p[1]), float(p[2])) for p in walk(w, 'xy')]
        for a, b in zip(pts, pts[1:]): union(a, b)
    for lbl in walk(sexp, 'label'):
        if len(lbl) > 1 and isinstance(lbl[1], str):
            at = next((n for n in walk(lbl, 'at')), None)
            if at: label_at[key(float(at[1]), float(at[2]))] = lbl[1]
    for lbl in walk(sexp, 'global_label'):
        if len(lbl) > 1 and isinstance(lbl[1], str):
            at = next((n for n in walk(lbl, 'at')), None)
            if at: label_at[key(float(at[1]), float(at[2]))] = lbl[1]
    # 3. Group.
    nets = defaultdict(lambda: {'labels': set(), 'pins': []})
    for k, pl in pin_at.items():
        nets[find(k)]['pins'].extend(pl)
    for k, name in label_at.items():
        nets[find(k)]['labels'].add(name)
    owner = {}
    dup = 0
    for root, n in nets.items():
        for pin in set(n['pins']):
            if pin in owner and owner[pin] != root:
                dup += 1
            owner[pin] = root
    if dup:
        print(f"WARN: {dup} pins appear in more than one net -- extraction unsound", file=sys.stderr)
    named = 0
    for root, n in sorted(nets.items(), key=lambda kv: -len(kv[1]['pins'])):
        if not n['pins']: continue
        label = '/'.join(sorted(n['labels'])) or '(unnamed)'
        if n['labels']: named += 1
        print(f"{label}: " + ', '.join(f"{r}.{num}({nam})" for r, num, nam in sorted(set(n['pins']))))
    print(f"# nets with pins: {sum(1 for n in nets.values() if n['pins'])}, labeled: {named}", file=sys.stderr)

if __name__ == '__main__':
    main(sys.argv[1])
