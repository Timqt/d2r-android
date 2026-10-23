#!/usr/bin/env python3
"""对比原指令与 Android 改写后的寄存器、标志和内存效果。
Compare original instructions with Android rewrites for register, flag, and memory effects.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as R

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
BIN = TOOLS / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
CASES = {
    "arithmetic": "mov x18, x1\nadd x18,x18,#7\nadd x0,x18,#9\nret",
    "conditional": "mov x18,x1\ncbz x18,1f\nmov x0,#22\nret\n1: mov x0,#11\nret",
    "test_bit": "mov x18,x1\ntbz x18,#2,1f\nmov x0,#22\nret\n1:mov x0,#11\nret",
    "flags": "mov x18,x1\nadds x18,x18,#7\nmrs x0,nzcv\nret",
    "pcrel": "adr x18,1f\nldr x0,[x18]\nret\n.p2align 3\n1:.quad 12345",
    "stack": "stp x18,x19,[sp,#-16]!\nmov x18,#321\nldp x18,x19,[sp],#16\nmov x0,x18\nret",
    "atomic": "1:ldxr x18,[x2]\nadd x18,x18,#1\nstxr w3,x18,[x2]\ncbnz w3,1b\nmov x0,x18\nret",
    "tls": "mrs x18,tpidr_el0\nmov x0,x18\nmsr tpidr_el0,x1\nmrs x2,tpidr_el0\nmrs x3,tpidrro_el0\nret",
}


def make_nso(code):
    text = (struct.pack("<II", 0x14000002, 0x4000) + code).ljust(0x1000, b"\0")
    header = bytearray(0x100); header[:4] = b"NSO0"
    for i, fields in enumerate([(0x100, 0, 0x1000), (0x1100, 0x4000, 0x100), (0x1200, 0x8000, 0x100)]):
        struct.pack_into("<III", header, 0x10 + i * 0x10, *fields)
    struct.pack_into("<I", header, 0x3c, 0x1000)
    struct.pack_into("<III", header, 0x60, 0x1000, 0x100, 0x100)
    ro = bytearray(0x100); ro[:4] = b"MOD0"
    struct.pack_into("<i", ro, 16, 0x20)
    struct.pack_into("<BBBBIIII", ro, 0x20, 1, 3, 3, 3, 0, 1, 0, 0)
    image = text + bytes(0x3000) + ro + bytes(0x3f00) + bytes(0x1100)
    return header + text + ro + bytes(0x100), image


def execute(image, seed, android=False, layout=None):
    base, context, stack, data, helper, stop = 0x100000, 0x500000, 0x600000, 0x700000, 0x800000, 0x900000
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(base, (len(image) + 0xfff) & ~0xfff); uc.mem_write(base, image)
    for address in [context, stack, data, helper, stop]: uc.mem_map(address, 0x10000)
    uc.mem_write(helper, struct.pack("<I", 0xd65f03c0))
    uc.mem_write(context, struct.pack("<8Q", seed, 0xabc000, 0xdef000, 0, 0, 0, 0, 0))
    uc.mem_write(data, struct.pack("<Q", 10))
    registers = [getattr(R, "UC_ARM64_REG_X" + str(i)) for i in range(29)] + [R.UC_ARM64_REG_X29, R.UC_ARM64_REG_X30]
    for i, register in enumerate(registers): uc.reg_write(register, 0x1000 + i)
    uc.reg_write(R.UC_ARM64_REG_X1, seed); uc.reg_write(R.UC_ARM64_REG_X2, data)
    uc.reg_write(R.UC_ARM64_REG_X18, 0x123456 if android else seed)
    uc.reg_write(R.UC_ARM64_REG_SP, stack + 0x8000); uc.reg_write(R.UC_ARM64_REG_X30, stop)
    uc.reg_write(R.UC_ARM64_REG_TPIDR_EL0, 0xabc000); uc.reg_write(R.UC_ARM64_REG_TPIDRRO_EL0, 0xdef000)
    uc.reg_write(R.UC_ARM64_REG_NZCV, 0xa0000000)
    if android:
        uc.mem_write(base + layout["KEY_PAGE_OFF"], struct.pack("<9Q", *[i * 8 for i in range(8)], helper))
        def hook(machine, address, size, user):
            if address == helper: machine.reg_write(R.UC_ARM64_REG_X16, context)
        uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(base, stop, count=20000)
    if uc.reg_read(R.UC_ARM64_REG_PC) != stop: raise AssertionError("Program did not return")
    values = [uc.reg_read(x) for x in registers]
    if android:
        values[18] = struct.unpack("<Q", uc.mem_read(context, 8))[0]
        tls = struct.unpack("<Q", uc.mem_read(context + 8, 8))[0]
        assert uc.reg_read(R.UC_ARM64_REG_X18) == 0x123456
        assert uc.reg_read(R.UC_ARM64_REG_TPIDR_EL0) == 0xabc000
    else:
        tls = uc.reg_read(R.UC_ARM64_REG_TPIDR_EL0)
    return values, uc.reg_read(R.UC_ARM64_REG_NZCV), uc.reg_read(R.UC_ARM64_REG_SP), bytes(uc.mem_read(data, 8)), tls


class RewriterTest(unittest.TestCase):
    def test_equivalence(self):
        for name, assembly in CASES.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp)
                (directory / "source.S").write_text(".text\n" + assembly + "\n")
                subprocess.run([str(BIN / "aarch64-linux-android29-clang"), "-c", str(directory / "source.S"), "-o", str(directory / "source.o")], check=True)
                subprocess.run([str(BIN / "llvm-objcopy"), "-O", "binary", "--only-section=.text", str(directory / "source.o"), str(directory / "source.bin")], check=True)
                nso, original = make_nso((directory / "source.bin").read_bytes())
                (directory / "probe.nso").write_bytes(nso)
                subprocess.run([sys.executable, str(ROOT.parent / "tools/patch_nso.py"), str(directory / "probe.nso"), str(directory), "probe"],
                               env=dict(os.environ, DM_TARGET="android", DM_RULESET="native"), check=True, stdout=subprocess.DEVNULL)
                layout = {line.split()[1]: int(line.split()[2], 16) for line in (directory / "probe.fixups.tsv").read_text().splitlines()}
                rewritten = (directory / "probe.patched.bin").read_bytes()
                for seed in [0, 4, 123, 0xffffffffffffffff]:
                    self.assertEqual(execute(original, seed), execute(rewritten, seed, True, layout))


if __name__ == "__main__": unittest.main()
