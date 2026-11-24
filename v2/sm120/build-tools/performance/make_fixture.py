"""Attach the v2 producer's required CPU midstate and job columns to CPU vectors."""
import argparse
import json
from pathlib import Path
import re

MASK = (1 << 32) - 1
COLS = ((0,4,8,12), (1,5,9,13), (2,6,10,14), (3,7,11,15),
        (0,5,10,15), (1,6,11,12), (2,7,8,13), (3,4,9,14))


def constants(header, name, count):
    found = re.search(r'\b' + name + r'\[[^=]+=\s*\{(.*?)\};', header, re.S)
    values = [int(value, 0) for value in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', found[1])]
    if len(values) != count:
        raise ValueError(f'Unexpected {name} in job snapshot header')
    return values


def rotr32(x, n):
    return (x >> n) | ((x << (32-n)) & MASK)


def g(v, index, x, y, ux, uy):
    a,b,c,d = index
    v[a] = (v[a] + v[b] + (x ^ uy)) & MASK
    v[d] = rotr32(v[d] ^ v[a], 16)
    v[c] = (v[c] + v[d]) & MASK
    v[b] = rotr32(v[b] ^ v[c], 12)
    v[a] = (v[a] + v[b] + (y ^ ux)) & MASK
    v[d] = rotr32(v[d] ^ v[a], 8)
    v[c] = (v[c] + v[d]) & MASK
    v[b] = rotr32(v[b] ^ v[c], 7)


def enrich(case, iv, table, sigma):
    words = case['words']
    v = [*iv, *table[:8]]
    v[12] ^= 512
    v[13] ^= 512
    for row in sigma[:14]:
        for i, index in enumerate(COLS):
            j,k = row[2*i:2*i+2]
            g(v, index, words[j], words[k], table[j], table[k])
    h = [iv[i] ^ v[i] ^ v[i+8] for i in range(8)]
    tail = words[16:20]
    v = [*h, *table[:8]]
    v[12] ^= 640
    v[13] ^= 640
    g(v, COLS[0], tail[0], tail[1], table[0], table[1])
    g(v, COLS[2], 0x80000000, 0, table[4], table[5])
    g(v, COLS[3], 0, 0, table[6], table[7])
    return dict(case, h=h, data=tail, record=[v[x] for group in (COLS[0],COLS[2],COLS[3]) for x in group])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--snapshot-header', type=Path, required=True)
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    header = a.snapshot_header.read_text()
    iv, table, flat = (constants(header, name, count)
                       for name, count in [('c_IV256', 8), ('c_u256', 16), ('c_sigma', 256)])
    sigma = [flat[i:i+16] for i in range(0, len(flat), 16)]
    cases = [enrich(case, iv, table, sigma) for case in json.loads(a.input.read_text())]
    a.output.write_text(json.dumps(dict(u256=table, sigma=sigma, cases=cases), indent=2) + '\n', encoding='ascii')


if __name__ == '__main__':
    main()
