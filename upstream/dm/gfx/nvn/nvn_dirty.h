// 客体内存写保护脏页跟踪（nvn_dirty.c）
// Write-protect dirty-page tracking for guest memory (nvn_dirty.c)
#pragma once
#include <stddef.h>
#include <stdint.h>

void nvn_dirty_add_range(const void *mem, size_t size);           // 登记一个内存池为跟踪区间（首次调用时注册缺页钩子与宿主写前钩子） | Register a memory pool as a tracked range (the first call registers the page-fault hook and the host pre-write hook)
void nvn_dirty_stat(int *live, int *peak, int *cap);                // 在册的内存池数、峰值、跟踪表容量（内存读数行用） | number of registered memory pools, peak, tracking table capacity (for the memory readout line)
void nvn_dirty_remove_range(const void *mem);                     // 池销毁：受保护页还原可写并标失效 | Pool destruction: restore protected pages to writable and mark invalid
int nvn_dirty_tracked(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len);              // 覆盖页设只读 | Set the covered pages read-only
uint64_t nvn_dirty_now(void);                                      // 当前代号：上传前取，作为该份内容的戳 | Current generation: taken before upload, as the stamp of that content
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp); // 覆盖页在戳之后被写过 → 1 | Covered pages were written after the stamp -> 1
typedef enum { NVN_DIRTY_DIAG_INVALID = 0, NVN_DIRTY_DIAG_UNTRACKED = 1, NVN_DIRTY_DIAG_TRACKED = 2 } nvn_dirty_diag_status;
typedef struct {
    uint64_t page_size, total_pages;
    uint64_t write_marked_pages, write_marked_bytes, write_marked_runs;
    uint64_t none_pages, prot_pages, busy_pages;
    uint64_t epoch_start, epoch_end;
    nvn_dirty_diag_status status;
} nvn_dirty_write_marked_stats;
// 只读诊断：gen > stamp 的页为写标记，不是内容差异；字节数裁剪到查询范围。零长度 / 溢出为 INVALID，完整单池覆盖才 TRACKED。
// Read-only diagnostic: gen > stamp denotes write marks, not content differences; bytes are clipped to the query. Zero length / overflow is INVALID, and only full single-pool coverage is TRACKED.
// 普通线程持元数据锁防销池换槽；不等 BUSY、不改保护或代数。两次全局 epoch 仅显示观测期间的写标记活动，不能证明内容一致或无并发。
// Normal threads hold the metadata lock against slot retirement/reuse; never wait on BUSY or change protection/generations. Global epochs only show observed write-mark activity, not coherent contents or absence of concurrency.
// 页状态 / 写标记计数仅在 TRACKED 时有效；诊断调用保持 errno，不可用于信号处理或授权源指针读取。
// State/write-mark counts are valid only when TRACKED; preserves errno and must not run in signal handlers or authorize source-pointer reads.
void nvn_dirty_diagnose_write_marks(const void *addr, size_t len, uint64_t old_stamp, nvn_dirty_write_marked_stats *out);
#define NVN_DIRTY_SNAPSHOT_MAX_RUNS 32
typedef struct { size_t offset, size; } nvn_dirty_snapshot_run;
enum {
    NVN_DIRTY_SNAPSHOT_OK, NVN_DIRTY_SNAPSHOT_INVALID, NVN_DIRTY_SNAPSHOT_UNTRACKED,
    NVN_DIRTY_SNAPSHOT_BUSY, NVN_DIRTY_SNAPSHOT_EPOCH, NVN_DIRTY_SNAPSHOT_RUN_LIMIT,
    NVN_DIRTY_SNAPSHOT_PROTECTION, NVN_DIRTY_SNAPSHOT_DISABLED, NVN_DIRTY_SNAPSHOT_REENTRANT,
    NVN_DIRTY_SNAPSHOT_STATE
};
typedef struct {
    uint64_t incarnation, stamp;
    size_t size;
    unsigned full, run_count;
    int reason, protection_errno;
    nvn_dirty_snapshot_run runs[NVN_DIRTY_SNAPSHOT_MAX_RUNS];
} nvn_dirty_snapshot_plan;
// 仅供新 GPU-only 谱系：调用方先持 queue 与宿主 snapshot-exclusive；不是信号入口或 CPU-ready 证明。
// Only for the new GPU-only lineage: caller already owns queue and host snapshot-exclusive; not a signal entry or CPU-ready proof.
// begin=1 固定元数据锁直到同线程 end/abort；begin=0 不持锁。old incarnation=0/不匹配时要求全量新基线。
// begin=1 pins metadata until same-thread end/abort; begin=0 holds no lock. Zero/mismatched old incarnation requires a fresh full baseline.
// 复制 plan.runs 后调用 end：仅返回 1 才可发布 incarnation/stamp；拒绝后的安全全读不得再保护或缓存。三接口保留 errno。
// After copying plan.runs call end: only 1 permits publishing incarnation/stamp; fallback safe full reads must not protect again or cache. All three preserve errno.
int nvn_dirty_snapshot_begin(const void *addr, size_t len, uint64_t old_stamp, uint64_t old_incarnation, nvn_dirty_snapshot_plan *plan);
int nvn_dirty_snapshot_end(nvn_dirty_snapshot_plan *plan);
void nvn_dirty_snapshot_abort(nvn_dirty_snapshot_plan *plan);
// 仅 post0：调用方持 queue，源为真实 GPU fence 后仍存活的 CPU-ready owned 输出，目的完整存活且与源不重叠。
// Only for post0: caller owns queue, source is live CPU-ready owned output after a real GPU fence, and the complete live destination does not overlap it.
// 1 = 已执行原 prepare hooks、真实 memcpy 和完成代数；0 = 完全未复制，调用方执行原 prepare+memcpy。保持 errno。
// 1 = original prepare hooks, actual memcpy and completion generations finished; 0 = no copy occurred, so caller uses original prepare+memcpy. Preserves errno.
int nvn_dirty_publish_copy(void *destination, const void *owned_readable_source, size_t size);
typedef struct {
    uint64_t calls, bytes, protected_pages, rw_calls, rw_ns, fallback_calls, rw_clock_unavailable;
} nvn_dirty_publish_copy_stats;
// 累计真实成功复制与批量 RW；protected_pages/rw_* 包含因后来 BUSY 而回退前已完成的权限操作，也是原 mprotect 计数的子集。
// Cumulative completed copies and actual batched RW; protected_pages/rw_* include completed permission work before a later BUSY fallback and are a subset of original mprotect counters.
void nvn_dirty_publish_copy_get_stats(nvn_dirty_publish_copy_stats *out);
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps);           // 累计：游戏写触发的缺页次数、宿主写前解保护的页数 | Cumulative: number of page faults triggered by game writes, number of pages unprotected before host writes
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns);   // 累计：本模块 mprotect 调用次数、覆盖页数、耗时 | Cumulative: this module's mprotect call count, pages covered, time spent
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total); // 当前存活槽、槽表高水位、登记总次数 | Currently live slots, slot table high-water mark, total number of registrations
