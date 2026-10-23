"""
Minimal ZIP rewriter that page-aligns every entry's DATA offset.

Needed because `zipalign` is a Linux-only binary and the build container keeps
spending 15+ minutes downloading openjdk. Android requires 4096-byte alignment
for uncompressed .so when the manifest sets extractNativeLibs="false"; the
original release APK has all 9 .so STORED and 4096-aligned, so it is set.

Alignment padding lives in the LOCAL header extra field (that is exactly what
zipalign does) using extra-field id 0xD935, the "Android ZIP alignment" id that
Android's own reader is documented to skip.
"""
import struct
import sys
import zlib
import zipfile

ALIGN = 4096
ALIGN_EXTRA_ID = 0xD935

LOCAL_SIG = 0x04034B50
CENTRAL_SIG = 0x02014B50
EOCD_SIG = 0x06054B50


def align_extra(offset, name_len, want_page):
    """Return the local extra-field bytes that put the data offset on a boundary."""
    if not want_page:
        return b""
    need = (-(offset + 30 + name_len)) % ALIGN
    if need == 0:
        return b""
    if need < 4:                      # an extra field needs id(2)+size(2)
        need += ALIGN
    return struct.pack("<HH", ALIGN_EXTRA_ID, need - 4) + b"\x00" * (need - 4)


def rewrite(src, dst):
    zin = zipfile.ZipFile(src, "r")
    offset = 0
    central = []

    with open(dst, "wb") as out:
        for info in zin.infolist():
            name = info.filename.encode("utf-8")
            raw = zin.read(info.filename)
            stored = info.compress_type == zipfile.ZIP_STORED
            if stored:
                method, payload = 0, raw
            else:
                co = zlib.compressobj(9, zlib.DEFLATED, -15)
                payload = co.compress(raw) + co.flush()
                method = 8

            crc = zlib.crc32(raw) & 0xFFFFFFFF
            extra = align_extra(offset, len(name), True)

            dos_time, dos_date = info.date_time[3] << 11 | info.date_time[4] << 5 | (info.date_time[5] // 2), \
                                 ((info.date_time[0] - 1980) << 9) | (info.date_time[1] << 5) | info.date_time[2]
            flags = 0x08 if name != info.filename.encode("utf-8") else 0  # we always store utf-8 names as-is

            out.write(struct.pack("<IHHHHHIIIHH", LOCAL_SIG, 20, flags, method,
                                  dos_time, dos_date, crc, len(payload), len(raw),
                                  len(name), len(extra)))
            out.write(name)
            out.write(extra)
            out.write(payload)

            central.append((info, name, extra, crc, len(payload), len(raw),
                            method, flags, dos_time, dos_date, offset))
            offset += 30 + len(name) + len(extra) + len(payload)

        cd_start = offset
        for (info, name, extra, crc, csz, usz, method, flags,
             dos_time, dos_date, local_off) in central:
            # the central copy must not carry the alignment padding
            out.write(struct.pack("<IHHHHHHIIIHHHHHII", CENTRAL_SIG, 20, 20, flags, method,
                                  dos_time, dos_date, crc, csz, usz,
                                  len(name), 0, 0, 0, 0, info.external_attr, local_off))
            out.write(name)
            offset += 46 + len(name)
        cd_size = offset - cd_start
        out.write(struct.pack("<IHHHHIIH", EOCD_SIG, 0, 0, len(central), len(central),
                              cd_size, cd_start, 0))

    zin.close()
    return len(central)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    n = rewrite(src, dst)
    print("rewrote %d entries -> %s" % (n, dst))

    # verify: reopen, test CRCs, and re-check data alignment from local headers
    z = zipfile.ZipFile(dst)
    bad = z.testzip()
    print("testzip:", "OK" if bad is None else "CORRUPT %s" % bad)
    data = open(dst, "rb").read()
    mis = []
    for i in z.infolist():
        off = i.header_offset
        sig, _, _, _, _, _, _, _, _, fnl, exl = struct.unpack_from("<IHHHHHIIIHH", data, off)
        d_off = off + 30 + fnl + exl
        if d_off % ALIGN:
            mis.append((i.filename, d_off))
    print("misaligned:", len(mis))
    for name, off in mis[:10]:
        print("   %-46s %d" % (name, off))
