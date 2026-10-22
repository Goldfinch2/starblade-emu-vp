#!/usr/bin/env python3
# c2xdasm.py <program.bin> <origin hex>: a TMS320C2x disassembler, a port of
# MAME's tms320c2x_dasm.cpp (its opcode table is read from that file).
# The program file is big-endian 16-bit words.
import re, sys

SRC = '/mnt/c/tmp/mame/src/devices/cpu/tms320c2x/tms320c2x_dasm.cpp'
text = open(SRC).read()
table = text[text.index('TMS320C2xFormats[] = {'):text.index('nullptr')]
pairs = re.findall(r'"([01a-z ]+)"\s*,\s*"([^"]*)"', table)

arith = ["*", "*-", "*+", "??", "BR0-", "*0-", "*0+", "*BR0+"]
nextar = ["", "", "", "", "", "", "", "", ",AR0", ",AR1", ",AR2", ",AR3", ",AR4", ",AR5", ",AR6", ",AR7"]
cmpmode = ["0 (ARx = AR0)", "1 (ARx < AR0)", "2 (ARx > AR0)", "3 (ARx <> AR0)"]

ops = []
for parse, fmt in pairs:
    p = parse.replace(' ', '')
    mask = bits = 0
    for i, ch in enumerate(p[:16]):
        b = 15 - i
        if ch == '1': mask |= 1 << b; bits |= 1 << b
        elif ch == '0': mask |= 1 << b
    ops.append((mask, bits, len(p) > 16, p, fmt))

def dasm(words, pc, origin):
    code = words[pc - origin]
    match = [o for o in ops if code & o[0] == o[1]]
    if not match:
        return 1, '???? dw %04Xh' % code
    mask, bits, ext, parse, fmt = match[-1]
    n = 1
    if ext:
        code = code << 16 | (words[pc - origin + 1] if pc - origin + 1 < len(words) else 0)
        n = 2
    width = len(parse)
    f = {}
    for i, ch in enumerate(parse):
        if ch.isalpha():
            f[ch] = f.get(ch, 0) << 1 | ((code >> (width - 1 - i)) & 1)
    out, i = '', 0
    while i < len(fmt):
        if fmt[i] == '%':
            k = fmt[i + 1]; i += 2
            v = f.get(k.lower(), 0)
            out += {'A': '%02Xh' % v, 'B': '%04Xh' % v, 'C': cmpmode[v & 3], 'D': '%02Xh' % v,
                    'K': '%d' % v, 'M': arith[v & 7], 'N': nextar[v & 15], 'P': 'PA$%01X' % v,
                    'R': 'AR%d' % v, 'S': ',%d' % v, 'T': '%01Xh' % v, 'W': '%04Xh' % v, 'X': ''}[k]
        else:
            out += fmt[i]; i += 1
    return n, out

def main():
    data = open(sys.argv[1], 'rb').read()
    origin = int(sys.argv[2], 16)
    words = [data[i] << 8 | data[i + 1] for i in range(0, len(data) - 1, 2)]
    pc = origin
    while pc - origin < len(words):
        n, s = dasm(words, pc, origin)
        raw = ' '.join('%04X' % words[pc - origin + j] for j in range(n) if pc - origin + j < len(words))
        print('%04X: %-10s %s' % (pc, raw, s))
        pc += n

main()
