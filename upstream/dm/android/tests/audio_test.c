// 使用原 HLE 队列测试 Android 的真实静音 PCM 回调、释放事件和暂停恢复。
// Test actual silent PCM callbacks, release events, and pause/resume through the original HLE queues.
#include "../../host/hle_audio.c"
#include "host_posix.h"
#include <assert.h>

static void *map_guest, *map_host;
static _Atomic unsigned releases;
void *objmap_get(const void *guest) { return guest == map_guest ? map_host : NULL; }
void objmap_put(const void *guest, void *host) { map_guest = (void *)guest; map_host = host; }
void *objmap_del(const void *guest) { assert(guest == map_guest); void *value = map_host; map_guest = map_host = NULL; return value; }
uint64_t now_ns(void) { return dm_android_clock_ns(CLOCK_MONOTONIC); }
void hle_signal_system_event(void *event) { (void)event; atomic_fetch_add(&releases, 1); }
void hle_bind_system_event(void *event, int autoclear) { (void)event; (void)autoclear; }

int main(void) {
    unsetenv("DM_AUDIO"); assert(real_audio());
    setenv("DM_AUDIO", "0", 1); assert(!real_audio());
    setenv("DM_AUDIO", "1", 1); assert(real_audio());
    // 真设备测试必须走应用的默认配置，不能用显式打开掩盖默认静音。
    // The device test must exercise app defaults, rather than mask default silence with explicit opt-in.
    unsetenv("DM_AUDIO");
    int16_t surround[] = {1000, 2000, 3000, 4000, 5000, 6000}, stereo[2] = {0};
    gbuf sample = {surround, sizeof surround, sizeof surround};
    aout conversion = {.ch = 6, .state = STATE_STARTED, .q_tail = 1, .notify_armed = 1};
    conversion.q[0] = &sample;
    au_render(NULL, &conversion, stereo, 1);
    assert(stereo[0] == 8656 && stereo[1] == 10363);
    assert(atomic_load(&conversion.render_frames) == 1);
    assert(atomic_load(&conversion.render_nonzero_samples) == 2);
    assert(atomic_load(&conversion.render_peak) == 10363);
    assert(atomic_load(&conversion.render_starved_frames) == 0);
    au_render(NULL, &conversion, stereo, 1);
    assert(!stereo[0] && !stereo[1]);
    assert(atomic_load(&conversion.render_starved_frames) == 1);
    uint8_t guest[128] = {0}, parameters[16] = {0};
    *(uint32_t *)parameters = 48000; *(uint16_t *)(parameters + 4) = 6;
    int event;
    assert(h_au_Open(guest, &event, "Default", parameters) == 0);
    assert(ao(guest)->au);
    gbuf buffers[8]; int16_t samples[8][1024 * 6] = {{0}};
    for (unsigned i = 0; i < 8; ++i) { h_au_SetBuf(&buffers[i], samples[i], sizeof samples[i], sizeof samples[i]); assert(h_au_Append(guest, &buffers[i])); }
    assert(h_au_Start(guest) == 0);
    for (unsigned i = 0; i < 100 && (!atomic_load(&releases) || dm_audio_render_calls() < 4); ++i) usleep(10000);
    assert(atomic_load(&releases)); assert(dm_audio_render_calls() >= 4); assert(h_au_Released(guest));
    dm_audio_suspend(1); usleep(50000); dm_audio_suspend(0);
    uint64_t before = dm_audio_render_calls(); usleep(100000); assert(dm_audio_render_calls() > before);
    h_au_Stop(guest); h_au_Close(guest);
    puts("Android AAudio HLE: PASS (default audio enabled, explicit mute honored, PCM counters, 48kHz PCM16, 6-to-2 channels, real callbacks, release events, pause/resume, teardown)");
    return 0;
}
