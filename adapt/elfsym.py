import struct, sys

def dump(path, filt=None):
    b = open(path, 'rb').read()
    assert b[:4] == b'\x7fELF', 'not ELF'
    is64 = b[4] == 2
    e_shoff, = struct.unpack_from('<Q', b, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', b, 0x3a)
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, link, info, align, entsz = struct.unpack_from('<IIQQQQIIQQ', b, o)
        secs.append(dict(name=name, typ=typ, off=off, size=size, link=link, entsz=entsz))
    shstr = secs[e_shstrndx]
    def sname(x):
        e = b.index(b'\0', shstr['off'] + x)
        return b[shstr['off']+x:e].decode()
    dynsym = dynstr = None
    for s in secs:
        n = sname(s['name'])
        if n == '.dynsym': dynsym = s
        if n == '.dynstr': dynstr = s
    needed = []
    for s in secs:
        if sname(s['name']) == '.dynamic':
            o = s['off']
            while True:
                tag, val = struct.unpack_from('<qQ', b, o)
                if tag == 0: break
                if tag == 1:
                    e = b.index(b'\0', dynstr['off'] + val)
                    needed.append(b[dynstr['off']+val:e].decode())
                o += 16
    print('=== %s ===' % path)
    print('  size=%d  SONAME/DT_NEEDED=%s' % (len(b), ', '.join(needed)))
    syms = []
    o = dynsym['off']; n = dynsym['size'] // 24
    for i in range(n):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', b, o)
        o += 24
        if st_name == 0: continue
        e = b.index(b'\0', dynstr['off'] + st_name)
        nm = b[dynstr['off']+st_name:e].decode('utf-8', 'replace')
        bind = st_info >> 4
        typ = st_info & 0xf
        kind = {0:'LOCAL',1:'GLOBAL',2:'WEAK'}.get(bind, str(bind))
        tname = {0:'NOTYPE',1:'OBJECT',2:'FUNC',3:'SECTION',10:'IFUNC'}.get(typ, str(typ))
        undef = 'U' if st_shndx == 0 else ' '
        syms.append((undef, kind, tname, nm))
    print('  total dynamic symbols: %d' % len(syms))
    defined = [s for s in syms if s[0] == ' ']
    print('  DEFINED: %d   UNDEFINED: %d' % (len(defined), len(syms) - len(defined)))
    if filt:
        rx = __import__('re').compile(filt)
        for s in syms:
            if rx.search(s[3]):
                print('    %s %-6s %-7s %s' % s)
    else:
        for s in defined:
            print('    %s %-6s %-7s %s' % s)
    print()

if __name__ == '__main__':
    f = sys.argv[2] if len(sys.argv) > 2 else None
    dump(sys.argv[1], f)
