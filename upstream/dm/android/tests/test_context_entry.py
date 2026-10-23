#!/usr/bin/env python3
"""执行真实专用入口机器码，验证 TLSDESC 解析时全部客体寄存器和状态不被破坏。
Execute the dedicated entry's actual machine code and verify all guest registers and state.

契约范围（2026-10-16 复核后修正）：本测试只针对**当前线程快路**汇编入口，它只读 TP 标签，
不做序号校验、也不用 dmb ishld；命中路径一条 msr 都不执行（那次提速正是省掉 NZCV 保存/恢复）。
快路的安全性不在入口里兜，而在写者一侧排除：登记拒绝一切已占用槽（含相同 TP），撤销仅限所有者
（见 native/runtime.c 与 runtime_context_cache.h）；任意 TP 查询那条seqlock 协议保留在
dm_android_context_cache_lookup。设备端 context_entry_test.c 对同一契约有等价断言。
Contract scope (corrected after the 2026-10-16 review): this test covers only the current-thread fast
path assembly entry, which reads the TP tag alone — no sequence check, no dmb ishld — and executes no
msr at all on a hit (dropping NZCV save/restore is what that speed-up was for). Fast-path safety is not
enforced inside the entry but excluded on the writer side: registration refuses any occupied slot (even
the same TP) and withdrawal is owner-only (see native/runtime.c and runtime_context_cache.h). The
seqlock protocol for arbitrary-TP queries remains in dm_android_context_cache_lookup. The device test
context_entry_test.c asserts the same contract.
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as R
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
BIN = TOOLS / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
MASK = (1 << 64) - 1
XREGS = [getattr(R, "UC_ARM64_REG_X" + str(i)) for i in range(31)]
QREGS = [getattr(R, "UC_ARM64_REG_Q" + str(i)) for i in range(32)]
STATE = [R.UC_ARM64_REG_SP, R.UC_ARM64_REG_NZCV, R.UC_ARM64_REG_FPSR,
         R.UC_ARM64_REG_FPCR, R.UC_ARM64_REG_TPIDR_EL0]


class ContextEntryTest(unittest.TestCase):
    def test_all_registers_and_thread_contexts(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            entry, library = temp / "entry.S", temp / "libcontext-test.so"
            subprocess.run([sys.executable, str(ROOT / "tools/generate_entry.py"), str(entry)], check=True)
            subprocess.run([str(BIN / "aarch64-linux-android29-clang"), "-shared", "-fPIC", "-O1",
                            "-I", str(ROOT / "native"), str(ROOT / "native/runtime.c"), str(entry),
                            str(ROOT / "tests/context_entry_probe.S"), str(ROOT / "tests/context_entry_test.c"),
                            "-Wl,-z,defs", "-ldl", "-o", str(library)], check=True)
            symbols = subprocess.check_output([str(BIN / "llvm-nm"), "-n", "--defined-only", str(library)], text=True)
            symbol_addr = {parts[2]: int(parts[0], 16) for line in symbols.splitlines()
                           if len(parts := line.split()) == 3}
            relocations = subprocess.check_output([str(BIN / "llvm-readelf"), "-r", str(library)], text=True)
            descriptors = re.findall(r"^([0-9a-f]+)\s+.*R_AARCH64_TLSDESC\s", relocations, re.M)
            self.assertEqual(len(descriptors), 1)
            descriptor = int(descriptors[0], 16)
            cache_address = symbol_addr["dm_android_context_cache"]
            cache_header = (ROOT / "native/runtime_context_cache.h").read_text()
            cache_bits = int(re.search(r"#define DM_ANDROID_CONTEXT_CACHE_BITS (\d+)", cache_header)[1])
            cache_hash_shift = int(re.search(r"#define DM_ANDROID_CONTEXT_CACHE_HASH_SHIFT (\d+)", cache_header)[1])
            cache_entry_shift = int(re.search(r"#define DM_ANDROID_CONTEXT_CACHE_ENTRY_SHIFT (\d+)", cache_header)[1])
            raw = library.read_bytes()
            phoff = struct.unpack_from("<Q", raw, 32)[0]
            phsize, phcount = struct.unpack_from("<HH", raw, 54)
            segments = [struct.unpack_from("<IIQQQQQQ", raw, phoff + i * phsize) for i in range(phcount)]
            loads = [s for s in segments if s[0] == 1]
            image_size = (max(s[3] + s[6] for s in loads) + 0xfff) & ~0xfff
            for seed in [0, 7, 123, 255]:
                for cache_mode in ["empty", "hit", "odd", "collision", "stale_tag"]:
                  for changed_status in [set(), {"nzcv"}, {"fpcr"}, {"fpsr"}, {"nzcv", "fpcr", "fpsr"}]:
                    with self.subTest(thread_seed=seed, cache=cache_mode, resolver_changes_status=changed_status):
                        base, stack, resolver, stop = 0x100000, 0x600000, 0x800000, 0x900000
                        tp, context = 0x2000000 + seed * 0x10000, 0x3000000 + seed * 0x20000
                        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                        uc.mem_map(base, image_size)
                        for segment in loads:
                            uc.mem_write(base + segment[3], raw[segment[2]:segment[2] + segment[5]])
                        for address in [stack, resolver, stop]:
                            uc.mem_map(address, 0x10000)
                        uc.mem_write(resolver, struct.pack("<I", 0xd65f03c0))  # ret
                        uc.mem_write(base + descriptor, struct.pack("<QQ", resolver, (context - tp) & MASK))
                        bucket = ((tp ^ (tp >> cache_hash_shift)) >> 4) & ((1 << cache_bits) - 1)
                        cache_slot = base + cache_address + (bucket << cache_entry_shift)
                        # 命中的两种写法：hit 是常规项；odd 的序号是奇数（像是写到一半），
                        # 但标签已是本线程 —— 入口只读标签，所以照旧命中（设备测试对同一契约有等价断言）。
                        # Two hit shapes: hit is a normal entry; odd has an odd sequence (as if mid-write)
                        # but the tag already belongs to this thread — the entry reads the tag alone, so it
                        # still hits (the device test asserts the same contract).
                        hit = cache_mode in ("hit", "odd")
                        if cache_mode != "empty":
                            # collision：标签对不上（另一位线程的 TP）；stale_tag：同桶里另一个 TP。
                            # 两者都必须落到慢路。并发改写由写者一侧排除，入口不需要再兜一次。
                            # collision: the tag does not match (another thread's TP); stale_tag: a different
                            # TP in the same bucket. Both must take the slow path. Concurrent rewrites are
                            # excluded on the writer side, so the entry does not need to guard again.
                            tag = {"collision": tp ^ (1 << 63), "stale_tag": tp ^ 0x1000}.get(cache_mode, tp)
                            sequence = 3 if cache_mode == "odd" else 2
                            uc.mem_write(cache_slot, struct.pack("<QQQQ", sequence, tag, context, 0))
                        for i, register in enumerate(XREGS):
                            uc.reg_write(register, (0x1234567890abcdef + seed * 31 + i) & MASK)
                        for i, register in enumerate(QREGS):
                            uc.reg_write(register, int.from_bytes(bytes((seed + i * 17 + j) % 256 for j in range(16)), "little"))
                        uc.reg_write(R.UC_ARM64_REG_X30, stop)
                        uc.reg_write(R.UC_ARM64_REG_SP, stack + 0x8000)
                        uc.reg_write(R.UC_ARM64_REG_TPIDR_EL0, tp)
                        uc.reg_write(R.UC_ARM64_REG_NZCV, 0xa0000000)
                        uc.reg_write(R.UC_ARM64_REG_FPSR, 0x1f)
                        uc.reg_write(R.UC_ARM64_REG_FPCR, 0x400000)
                        uc.mem_write(stack + 0x8000, bytes(range(128)))
                        before = {reg: uc.reg_read(reg) for reg in XREGS + QREGS + STATE}
                        calls, status_writes = [], []
                        decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

                        def resolve(machine, address, size, user):
                            if address == resolver:
                                self.assertEqual(machine.reg_read(R.UC_ARM64_REG_X0), base + descriptor)
                                machine.reg_write(R.UC_ARM64_REG_X0, (context - tp) & MASK)
                                if "nzcv" in changed_status:
                                    machine.reg_write(R.UC_ARM64_REG_NZCV, 0)
                                if "fpsr" in changed_status:
                                    machine.reg_write(R.UC_ARM64_REG_FPSR, 0)
                                if "fpcr" in changed_status:
                                    machine.reg_write(R.UC_ARM64_REG_FPCR, 0)
                                calls.append(address)
                            else:
                                instruction = next(decoder.disasm(bytes(machine.mem_read(address, size)), address))
                                if instruction.mnemonic == "msr":
                                    status_writes.append(instruction.op_str.split(",")[0])

                        uc.hook_add(UC_HOOK_CODE, resolve)
                        uc.emu_start(base + symbol_addr["dm_android_context_entry"], stop, count=1000)
                        self.assertEqual(uc.reg_read(R.UC_ARM64_REG_PC), stop)
                        self.assertEqual(len(calls), 0 if hit else 1)
                        for register, value in before.items():
                            if register != R.UC_ARM64_REG_X16:
                                self.assertEqual(uc.reg_read(register), value, f"register {register}")
                        self.assertEqual(uc.reg_read(R.UC_ARM64_REG_X16), context)
                        self.assertEqual(bytes(uc.mem_read(stack + 0x8000, 128)), bytes(range(128)))
                        # 命中路径一条 msr 都不该有 —— 这正是 2026-10-16 那次提速要省掉的东西；
                        # 慢路每个寄存器最多恢复一次，且只在解析器真的改动过它时。
                        # The hit path must contain no msr at all — dropping exactly this is what the
                        # 2026-10-16 speed-up was for. The slow path restores each register at most once,
                        # and only when the resolver actually changed it.
                        self.assertEqual(status_writes.count("fpcr"), int(not hit and "fpcr" in changed_status))
                        self.assertEqual(status_writes.count("fpsr"), int(not hit and "fpsr" in changed_status))
                        self.assertEqual(status_writes.count("nzcv"), int(not hit))
                        if hit:
                            self.assertEqual(status_writes, [], "cache-hit path must not write any status register")
                            # 入口是纯读者：连序号都不许动。
                            # The entry is a pure reader: it must not touch even the sequence.
                            self.assertEqual(struct.unpack_from("<Q", uc.mem_read(cache_slot, 8))[0],
                                             3 if cache_mode == "odd" else 2, "entry must not modify the cache entry")


if __name__ == "__main__":
    unittest.main()
