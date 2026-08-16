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
    # 1. Library pin geometry: lib_id -> [(number, name, x, y)]
    libpins = {}
    for lib in walk(sexp, 'symbol'):
        if not (len(lib) > 1 and isinstance(lib[1], str)): continue
        pins = []
        for p in walk(lib, 'pin'):
            at = next(walk(p, 'at'), None)
            num = next(walk(p, 'number'), None)
            nam = next(walk(p, 'name'), None)
            if at and num:
                pins.append((num[1], nam[1] if nam else '', float(at[1]), float(at[2])))
        if pins and '.' not in str(lib[1]):  # top-level lib defs only… keep all
            libpins.setdefault(lib[1], pins)
    # 2. Points registry with union-find.
    parent = {}
    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    def union(a, b): parent[find(a)] = find(b)
    def key(x, y): return (round(x * 100), round(y * 100))
    pin_at = {}   # point -> [(ref, pinnum, pinname)]
    label_at = {} # point -> label
    # Instances: (symbol (lib_id "X") (at x y rot) [mirror] … (property "Reference" "U1"))
    for inst in walk(sexp, 'symbol'):
        libid = next((n[1] for n in walk(inst, 'lib_id') if len(n) > 1), None)
        if not libid: continue
        at = next((n for n in walk(inst, 'at') if len(n) >= 3), None)
        if not at: continue
        ref = next((n[2] for n in walk(inst, 'property') if len(n) > 2 and n[1] == 'Reference'), '?')
        if ref.startswith('#'): continue
        ox, oy, rot = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
        mir = next((n[1] for n in walk(inst, 'mirror') if len(n) > 1), None)
        base = libid.split(':')[-1]
        pins = libpins.get(libid) or libpins.get(base) or next((v for k, v in libpins.items() if k.endswith(base)), [])
        for num, nam, px, py in pins:
            x, y = px, py
            if mir == 'y': x = -x
            if mir == 'x': y = -y
            r = math.radians(rot)
            # schematic Y grows down; symbol pin Y is up-positive -> negate.
            rx = x * math.cos(r) + y * math.sin(r)
            ry = -(-x * math.sin(r) + y * math.cos(r))
            k = key(ox + rx, oy + ry)
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
    named = 0
    for root, n in sorted(nets.items(), key=lambda kv: -len(kv[1]['pins'])):
        if not n['pins']: continue
        label = '/'.join(sorted(n['labels'])) or '(unnamed)'
        if n['labels']: named += 1
        print(f"{label}: " + ', '.join(f"{r}.{num}({nam})" for r, num, nam in sorted(set(n['pins']))))
    print(f"# nets with pins: {sum(1 for n in nets.values() if n['pins'])}, labeled: {named}", file=sys.stderr)

if __name__ == '__main__':
    main(sys.argv[1])
