// 平台信号现场的寄存器访问。| Register access in platform signal contexts.
#pragma once
#include <stdint.h>
#include <string.h>
#include <sys/ucontext.h>
#ifdef __ANDROID__
#define DM_UC_PC(uc) ((uc)->uc_mcontext.pc)
#define DM_UC_SP(uc) ((uc)->uc_mcontext.sp)
#define DM_UC_REG(uc, index) ((uc)->uc_mcontext.regs[(index)])
#define DM_UC_FP(uc) DM_UC_REG(uc, 29)
#define DM_UC_LR(uc) DM_UC_REG(uc, 30)
static inline int dm_uc_is_write(const ucontext_t *uc) {
    const uint8_t *data = (const void *)uc->uc_mcontext.__reserved;
    size_t size = sizeof uc->uc_mcontext.__reserved;
    for (size_t offset = 0; offset + 8 <= size;) {
        uint32_t magic, length;
        memcpy(&magic, data + offset, 4); memcpy(&length, data + offset + 4, 4);
        if (!magic || length < 8 || length > size - offset) break;
        if (magic == 0x45535201 && length >= 16) { uint64_t esr; memcpy(&esr, data + offset + 8, 8); return (esr >> 6) & 1; }
        offset += length;
    }
    return 0;
}
#else
#define DM_UC_PC(uc) ((uc)->uc_mcontext->__ss.__pc)
#define DM_UC_SP(uc) ((uc)->uc_mcontext->__ss.__sp)
#define DM_UC_REG(uc, index) ((uc)->uc_mcontext->__ss.__x[(index)])
#define DM_UC_FP(uc) ((uc)->uc_mcontext->__ss.__fp)
#define DM_UC_LR(uc) ((uc)->uc_mcontext->__ss.__lr)
static inline int dm_uc_is_write(const ucontext_t *uc) { return (uc->uc_mcontext->__es.__esr >> 6) & 1; }
#endif
