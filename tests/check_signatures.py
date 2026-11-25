"""Check the probe's AOB signatures against the user's Cheat Engine dumps.

Builds a byte map (module offset -> byte) from CE disassembly text, then checks each
signature matches exactly once, at the expected offset, and that the rip-relative
operands it decodes give the addresses we already confirmed in game.
Usage: python3 -I check_signatures.py <dump.txt> [<dump.txt> ...]
"""
import re, sys

AXA = 0x14F1E0   # axaAppMain = exe+0x14F1E0 (proved by the queue slot)

def load(paths):
    mem = {}
    pat = re.compile(r'KINGDOM HEARTS II FINAL MIX\.(exe|axaAppMain)\+([0-9A-Fa-f]+)\s+-\s+((?:[0-9A-Fa-f]{2,}\s?)+)')
    for p in paths:
        for line in open(p, encoding='utf-8', errors='replace'):
            m = pat.search(line)
            if not m:
                continue
            base = 0 if m.group(1) == 'exe' else AXA
            off = base + int(m.group(2), 16)
            hexstr = ''.join(m.group(3).split())
            for i in range(0, len(hexstr), 2):
                mem[off + i // 2] = int(hexstr[i:i + 2], 16)
    return mem

def parse(sig):
    return [None if t == '??' else int(t, 16) for t in sig.split()]

def find(mem, sig):
    p = parse(sig)
    hits = []
    for start in sorted(mem):
        ok = True
        for i, b in enumerate(p):
            v = mem.get(start + i)
            if v is None or (b is not None and v != b):
                ok = False
                break
        if ok:
            hits.append(start)
    return hits

def rip(mem, instr_off, disp_at, instr_len):
    d = int.from_bytes(bytes(mem[instr_off + disp_at + i] for i in range(4)), 'little', signed=True)
    return instr_off + instr_len + d

# Keep these strings identical to the C source (copied by tests below).
SIGS = {
    'OpenMenu': ('80 3D ?? ?? ?? ?? 00 74 0F 39 0D ?? ?? ?? ?? 74 17 89 0D ?? ?? ?? ?? C3 '
                 '89 0D ?? ?? ?? ?? C7 05 ?? ?? ?? ?? FF FF FF FF C3', 0x151770),
    'StartCheck': ('F6 40 08 02 74 2B E8 ?? ?? ?? ?? 48 85 C0 ?? ?? E8 ?? ?? ?? ?? 84 C0 75 18 '
                   'BA FF FF FF FF 33 C9 E8', 0x1572F9),
    'PauseSelect': ('83 F9 05 75 05 E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? C1 E8 02 A8 01 74 0E '
                    'F7 C6 F7 FF FF FF B8 0F 00 00 00 0F 44 F0 33 DB 4C 8D 25 ?? ?? ?? ??', 0x2E4983),
    'PauseTail': ('0F BF 4B 02 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 7C 24 50', 0x2E4AF6),
}

def main(paths):
    mem = load(paths)
    print(f'{len(mem)} dumped bytes loaded')
    fails = 0
    def check(cond, msg):
        nonlocal fails
        print(('PASS ' if cond else 'FAIL ') + msg)
        fails += 0 if cond else 1

    res = {}
    for name, (sig, exp) in SIGS.items():
        hits = find(mem, sig)
        res[name] = hits[0] if len(hits) == 1 else None
        check(len(hits) == 1 and hits[0] == exp,
              f'{name}: {len(hits)} match(es) {[hex(h) for h in hits]}, expected {hex(exp)}')

    om = res['OpenMenu']
    if om is not None:
        check(rip(mem, om + 0x00, 2, 7) == 0x9006B0, 'OpenMenu -> menu-open byte 0x9006B0')
        check(rip(mem, om + 0x09, 2, 6) == 0x716B48, 'OpenMenu -> queue 0x716B48')
        check(rip(mem, om + 0x18, 2, 6) == 0x900724, 'OpenMenu -> currentmenu 0x900724')
    sc = res['StartCheck']
    if sc is not None:
        site = sc + 0x0E
        check(site == 0x157307, f'StartCheck -> patch site {hex(site)}')
        check((mem[site], mem[site + 1]) in ((0x75, 0x17), (0x90, 0x90)), 'StartCheck site bytes are 75 17 or 90 90')
        check(rip(mem, sc + 0x06, 1, 5) == 0x3AD540, 'StartCheck -> special-pause lookup exe+3AD540')
    ps = res['PauseSelect']
    if ps is not None:
        check(rip(mem, ps + 0x0A, 2, 6) == 0x749830, 'PauseSelect -> mode bits 0x749830')
        check(rip(mem, ps + 0x27, 3, 7) == 0x5BA170, 'PauseSelect -> pause table 0x5BA170')

    pt = res['PauseTail']
    if pt is not None:
        check(rip(mem, pt + 4, 1, 5) == 0x151770, 'PauseTail -> first call is OpenMenu')
        check(rip(mem, pt + 9, 1, 5) == 0x151480, 'PauseTail -> follow-up exe+151480')
    if sc is not None:
        check(bytes(mem[sc + 0x19 + i] for i in range(5)) == bytes([0xBA, 0xFF, 0xFF, 0xFF, 0xFF]), 'hook site (StartCheck+0x19) is mov edx,-1')
        check(bytes(mem[sc + 0x31 + i] for i in range(5)) == bytes([0x33, 0xD2, 0x48, 0x8B, 0xCB]), 'loop site (StartCheck+0x31) is xor edx,edx / mov rcx,rbx')
        check(bytes(mem[sc + 0x0B + i] for i in range(5)) == bytes([0x48, 0x85, 0xC0, 0x75, 0x17]), 'special site (StartCheck+0x0B) is test rax,rax / jne')
        check(bytes(mem[sc + 0x27 + i] for i in range(5)) == bytes([0x33, 0xD2, 0x48, 0x8B, 0xC8]), 'special path (StartCheck+0x27) is xor edx,edx / mov rcx,rax')

    # Mutation tests: the checks must be able to fail.
    bad = dict(mem); bad[0x151770] ^= 0xFF
    check(len(find(bad, SIGS['OpenMenu'][0])) == 0, 'mutation: corrupt OpenMenu byte -> no match')
    bad = dict(mem); bad[0x1572F9 + 0x17] ^= 0xFF
    check(len(find(bad, SIGS['StartCheck'][0])) == 0, 'mutation: corrupt StartCheck byte -> no match')
    bad = dict(mem); bad[0x157307] = 0x90; bad[0x157308] = 0x90
    check(len(find(bad, SIGS['StartCheck'][0])) == 1, 'mutation: Lua patch applied (90 90) -> still matches')

    print('ALL PASS' if fails == 0 else f'{fails} FAILED')
    return fails

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
