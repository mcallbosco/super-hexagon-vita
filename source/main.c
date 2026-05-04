#ifdef STANDALONE_DEMO

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

int _newlib_heap_size_user = 16 * 1024 * 1024;

#define FB_WIDTH 960
#define FB_HEIGHT 544
#define FB_STRIDE 960
#define CENTER_X (FB_WIDTH / 2)
#define CENTER_Y (FB_HEIGHT / 2)
#define PI 3.14159265358979323846f

static uint32_t *front;
static uint32_t *back;
static float player_angle = 0.0f;
static float speed = 1.3f;
static uint32_t frame_no = 0;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static void clear(uint32_t *fb, uint32_t color) {
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; ++i) fb[i] = color;
}

static void putpx(uint32_t *fb, int x, int y, uint32_t color) {
    if ((unsigned)x < FB_WIDTH && (unsigned)y < FB_HEIGHT) fb[y * FB_STRIDE + x] = color;
}

static float edge_for_angle(float x, float y, float a) {
    float c = cosf(a);
    float s = sinf(a);
    return x * c + y * s;
}

static void draw_regular_poly(uint32_t *fb, float radius, float rotation, uint32_t color, int thickness) {
    for (int y = 0; y < FB_HEIGHT; ++y) {
        float py = (float)y - CENTER_Y;
        for (int x = 0; x < FB_WIDTH; ++x) {
            float px = (float)x - CENTER_X;
            float max_edge = -99999.0f;
            for (int i = 0; i < 6; ++i) {
                float e = edge_for_angle(px, py, rotation + (PI / 6.0f) + i * PI / 3.0f);
                if (e > max_edge) max_edge = e;
            }
            float d = radius - max_edge;
            if (d >= 0.0f && d < thickness) putpx(fb, x, y, color);
        }
    }
}

static void draw_line(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int oy = -2; oy <= 2; ++oy) for (int ox = -2; ox <= 2; ++ox) putpx(fb, x0 + ox, y0 + oy, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_player(uint32_t *fb) {
    float a = player_angle;
    int x0 = CENTER_X + (int)(cosf(a) * 64.0f);
    int y0 = CENTER_Y + (int)(sinf(a) * 64.0f);
    int x1 = CENTER_X + (int)(cosf(a + 0.18f) * 104.0f);
    int y1 = CENTER_Y + (int)(sinf(a + 0.18f) * 104.0f);
    int x2 = CENTER_X + (int)(cosf(a - 0.18f) * 104.0f);
    int y2 = CENTER_Y + (int)(sinf(a - 0.18f) * 104.0f);
    draw_line(fb, x0, y0, x1, y1, rgb(255, 255, 255));
    draw_line(fb, x1, y1, x2, y2, rgb(255, 255, 255));
    draw_line(fb, x2, y2, x0, y0, rgb(255, 255, 255));
}

static void draw_center(uint32_t *fb) {
    for (int y = -20; y <= 20; ++y) {
        for (int x = -20; x <= 20; ++x) {
            if (x * x + y * y <= 400) putpx(fb, CENTER_X + x, CENTER_Y + y, rgb(20, 20, 20));
        }
    }
}

static void render(uint32_t *fb) {
    uint8_t r = (uint8_t)(80 + 40 * sinf(frame_no * 0.025f));
    uint8_t g = (uint8_t)(35 + 30 * sinf(frame_no * 0.017f + 2.0f));
    uint8_t b = (uint8_t)(120 + 45 * sinf(frame_no * 0.021f + 4.0f));
    clear(fb, rgb(r, g, b));

    float rot = frame_no * 0.018f;
    for (int i = 0; i < 7; ++i) {
        uint32_t color = (i & 1) ? rgb(240, 240, 240) : rgb(40, 40, 40);
        draw_regular_poly(fb, 76.0f + i * 48.0f, rot + i * 0.18f, color, 8);
    }

    float oa = -rot * 1.7f;
    for (int lane = 0; lane < 6; lane += 2) {
        float a = oa + lane * PI / 3.0f;
        draw_line(fb, CENTER_X + cosf(a) * 120, CENTER_Y + sinf(a) * 120,
                  CENTER_X + cosf(a) * 430, CENTER_Y + sinf(a) * 430, rgb(20, 20, 20));
    }

    draw_center(fb);
    draw_player(fb);
}

int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceUID mem = sceKernelAllocMemBlock("shx_demo_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                        FB_WIDTH * FB_HEIGHT * 4 * 2, NULL);
    void *base = NULL;
    sceKernelGetMemBlockBase(mem, &base);
    front = (uint32_t *)base;
    back = front + FB_WIDTH * FB_HEIGHT;

    SceDisplayFrameBuf dbuf = {0};
    dbuf.size = sizeof(dbuf);
    dbuf.base = front;
    dbuf.pitch = FB_STRIDE;
    dbuf.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    dbuf.width = FB_WIDTH;
    dbuf.height = FB_HEIGHT;

    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_LEFT) player_angle -= 0.055f * speed;
        if (pad.buttons & SCE_CTRL_RIGHT) player_angle += 0.055f * speed;
        if (pad.lx < 90) player_angle -= 0.045f * speed;
        if (pad.lx > 166) player_angle += 0.045f * speed;
        if (pad.buttons & SCE_CTRL_CROSS) speed = 2.1f; else speed = 1.3f;

        render(back);
        dbuf.base = back;
        sceDisplaySetFrameBuf(&dbuf, SCE_DISPLAY_SETBUF_NEXTFRAME);
        sceDisplayWaitVblankStart();
        uint32_t *tmp = front; front = back; back = tmp;
        ++frame_no;
    }

    sceKernelExitProcess(0);
    return 0;
}

#else

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/settings.h"
#include "utils/utils.h"

#include <reimpl/controls.h>
#include <reimpl/asset_manager.h>
#include <reimpl/android_stubs.h>
#include <reimpl/native_audio.h>
#include <reimpl/native_input.h>
#include <reimpl/native_text.h>

#include <falso_jni/FalsoJNI.h>
#include <fios/fios.h>
#include <kubridge.h>
#include <so_util/so_util.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;
so_module libcxx_mod;
static so_module oboe_mod;
static so_module of_mod;
static void (*shx_set_elapsed_ms)(const uint64_t *) = NULL;
static void (*shx_set_last_frame_ms)(const double *) = NULL;
static void (*shx_set_expected_delta)(const float *) = NULL;
static float (*shx_get_expected_delta)(void) = NULL;
static float (*of_get_frame_rate)(void) = NULL;
static float (*of_get_target_frame_rate)(void) = NULL;
static uint64_t shx_time_origin_us = 0;
static uint64_t shx_last_frame_us = 0;
static uint32_t shx_timing_diag_frames = 0;
static uint32_t shx_force_fps_frames = 0;
static void *(*of_get_app_ptr_cached)(void) = NULL;
static so_hook shx_update_hook;
static so_hook shx_calculate_time_hook;
static so_hook shx_gameinput_hook;
static so_hook shx_gamelogic_hook;
static so_hook shx_initfov_hook;
static uint32_t shx_update_count = 0;
static uint32_t shx_calculate_time_count = 0;
static uint32_t shx_gamelogic_count = 0;
static uint32_t shx_render_count = 0;
static void *shx_last_game_self = NULL;
static void *shx_last_gameclass_self = NULL;

#define DIAG_PATH DATA_PATH "native_diag.txt"
#define GAME_SPEED_SCALE 1.0
#define GAME_VIEW_WIDTH 960
#define GAME_VIEW_HEIGHT 544
#define GAME_VIEW_X(x) ((float)(x) * ((float)GAME_VIEW_WIDTH / 960.0f))
#define GAME_VIEW_Y(y) ((float)(y) * ((float)GAME_VIEW_HEIGHT / 544.0f))

void resolve_imports(so_module *mod);
void so_patch(void);
int ret0(void);

static void load_reloc_resolve(so_module *mod, const char *path, uintptr_t base) {
    l_info("Loading %s at 0x%08x", path, (unsigned)base);
    int res = so_file_load(mod, path, base);
    if (res < 0) fatal_error("Failed to load %s: 0x%08X", path, res);
    so_relocate(mod);
    resolve_imports(mod);
    so_flush_caches(mod);
}

static void init_module(so_module *mod, const char *name) {
    l_info("Running init array for %s", name);
    so_initialize(mod);
    so_flush_caches(mod);
}

static void call_void_jni(so_module *mod, const char *sym) {
    void (*fn)(JNIEnv *, jclass) = (void *)so_symbol(mod, sym);
    if (fn) {
        l_info("Calling %s", sym);
        fn(&jni, NULL);
    } else {
        l_warn("Missing JNI symbol %s", sym);
    }
}

static void diag_append_main(const char *msg) {
#ifdef DEBUG_SOLOADER
    SceUID fd = sceIoOpen(DIAG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (int)strlen(msg));
        sceIoClose(fd);
    }
#else
    (void)msg;
#endif
}

static void continue_void_hook(so_hook *h, void *arg0) {
    kuKernelCpuUnrestrictedMemcpy((void *)h->addr, h->orig_instr, sizeof(h->orig_instr));
    kuKernelFlushCaches((void *)h->addr, sizeof(h->orig_instr));
    if (h->thumb_addr) ((void (*)(void *))h->thumb_addr)(arg0);
    else ((void (*)(void *))h->addr)(arg0);
    kuKernelCpuUnrestrictedMemcpy((void *)h->addr, h->patch_instr, sizeof(h->patch_instr));
    kuKernelFlushCaches((void *)h->addr, sizeof(h->patch_instr));
}

static void native_superhex_update_probe(void *self) {
    shx_update_count++;
    continue_void_hook(&shx_update_hook, self);
}

static void native_superhex_calculate_time_probe(void *self) {
    shx_calculate_time_count++;
    continue_void_hook(&shx_calculate_time_hook, self);
}

static void native_superhex_gameinput_probe(void *self) {
    shx_last_game_self = self;
    *(double *)((char *)self + 0x2998) = 1.0;
    continue_void_hook(&shx_gameinput_hook, self);
}

static void native_superhex_gamelogic_probe(void *self) {
    shx_gamelogic_count++;
    shx_last_game_self = self;
    *(double *)((char *)self + 0x2998) = 1.0;
    continue_void_hook(&shx_gamelogic_hook, self);
}

static void native_gameclass_initfov_probe(void *self) {
    continue_void_hook(&shx_initfov_hook, self);
    shx_last_gameclass_self = self;
    char *fov = (char *)self + 0x5698;
    int normal_value = *(int *)(fov + 4);
    int adjusted_value = normal_value;
    *(int *)(fov + 4) = adjusted_value;
    char line[160];
    snprintf(line, sizeof(line), "fov_diag self=%p mode=%d normal=%d value=%d a=%f b=%f\n",
             self, *(int *)fov, normal_value, adjusted_value, (float)*(double *)(fov + 136), (float)*(double *)(fov + 144));
    diag_append_main(line);
}

static float native_of_get_last_frame_time(void) {
    return (float)(GAME_SPEED_SCALE / 60.0);
}

static float native_of_get_frame_rate(void) {
    return 60.0f;
}

static float native_of_get_target_frame_rate(void) {
    return 60.0f;
}

static uint64_t native_of_get_elapsed_time_millis(void) {
    if (!shx_time_origin_us) return 0;
    uint64_t now_us = (uint64_t)sceKernelGetSystemTimeWide();
    return (now_us - shx_time_origin_us) / 1000;
}

uint64_t native_port_elapsed_ms(void) {
    return native_of_get_elapsed_time_millis();
}

static void install_of_timing_hooks(void) {
    uintptr_t a;
    if ((a = so_symbol(&of_mod, "_Z18ofGetLastFrameTimev"))) hook_addr(a, (uintptr_t)&native_of_get_last_frame_time);
    if ((a = so_symbol(&of_mod, "_Z14ofGetFrameRatev"))) hook_addr(a, (uintptr_t)&native_of_get_frame_rate);
    if ((a = so_symbol(&of_mod, "_Z20ofGetTargetFrameRatev"))) hook_addr(a, (uintptr_t)&native_of_get_target_frame_rate);
    if ((a = so_symbol(&of_mod, "_Z22ofGetElapsedTimeMillisv"))) hook_addr(a, (uintptr_t)&native_of_get_elapsed_time_millis);
}

static void install_timing_probe_hooks(void) {
    uintptr_t a;
    if ((a = so_symbol(&so_mod, "_ZN8superhex6updateEv"))) {
        shx_update_hook = hook_addr(a, (uintptr_t)&native_superhex_update_probe);
    }
    if ((a = so_symbol(&so_mod, "_ZN8superhex13calculateTimeEv"))) {
        shx_calculate_time_hook = hook_addr(a, (uintptr_t)&native_superhex_calculate_time_probe);
    }
    if ((a = so_symbol(&so_mod, "_ZN8superhex9gameinputEv"))) {
        shx_gameinput_hook = hook_addr(a, (uintptr_t)&native_superhex_gameinput_probe);
    }
    if ((a = so_symbol(&so_mod, "_ZN8superhex9gamelogicEv"))) {
        shx_gamelogic_hook = hook_addr(a, (uintptr_t)&native_superhex_gamelogic_probe);
    }
    if ((a = so_symbol(&so_mod, "_ZN9gameclass7initfovEv"))) {
        shx_initfov_hook = hook_addr(a, (uintptr_t)&native_gameclass_initfov_probe);
    }
}

static void call_setup(so_module *mod) {
    void (*setup)(JNIEnv *, jclass, jint, jint) = (void *)so_symbol(mod, "Java_cc_openframeworks_OFAndroid_setup");
    if (setup) setup(&jni, NULL, GAME_VIEW_WIDTH, GAME_VIEW_HEIGHT);
}

static void call_resize(so_module *mod) {
    void (*resize)(JNIEnv *, jclass, jint, jint) = (void *)so_symbol(mod, "Java_cc_openframeworks_OFAndroid_resize");
    if (resize) resize(&jni, NULL, GAME_VIEW_WIDTH, GAME_VIEW_HEIGHT);
}

static void call_render(so_module *mod) {
    void (*render)(JNIEnv *, jclass) = (void *)so_symbol(mod, "Java_cc_openframeworks_OFAndroid_render");
    if (render) render(&jni, NULL);
}

static void send_key(so_module *mod, int down, int keycode, int unicode) {
    const char *sym = down ? "Java_cc_openframeworks_OFAndroid_onKeyDown" : "Java_cc_openframeworks_OFAndroid_onKeyUp";
    jboolean (*fn)(JNIEnv *, jclass, jint, jint) = (void *)so_symbol(mod, sym);
    if (fn) fn(&jni, NULL, keycode, unicode);
}

static void send_touch_event(so_module *mod, const char *sym, int id, float x, float y) {
    void (*fn)(JNIEnv *, jclass, jint, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat) = (void *)so_symbol(mod, sym);
    if (fn) fn(&jni, NULL, id, x, y, 1.0f, 8.0f, 8.0f, 0.0f);
}

static void send_touch(so_module *mod, int down, int id, float x, float y) {
    send_touch_event(mod, down ? "Java_cc_openframeworks_OFAndroid_onTouchDown" : "Java_cc_openframeworks_OFAndroid_onTouchUp", id, x, y);
}

static void send_tap(so_module *mod, int id, float x, float y) {
    send_touch(mod, 1, id, x, y);
    send_touch(mod, 0, id, x, y);
}

static void send_touch_moved(so_module *mod, int id, float x, float y) {
    send_touch_event(mod, "Java_cc_openframeworks_OFAndroid_onTouchMoved", id, x, y);
}

static void send_axis(so_module *mod, int axis, float value) {
    void (*fn)(JNIEnv *, jclass, jint, jint, jfloat, jfloat) = (void *)so_symbol(mod, "Java_cc_openframeworks_OFAndroid_onAxisMoved");
    if (fn) fn(&jni, NULL, 0, axis, value, 0.0f);
}

static void init_game_timing(void) {
    shx_set_elapsed_ms = (void *)so_symbol(&so_mod, "_Z20setElapsedTimeMillisRKy");
    shx_set_last_frame_ms = (void *)so_symbol(&so_mod, "_Z22setLastFrameTimeMillisRKd");
    shx_set_expected_delta = (void *)so_symbol(&so_mod, "_Z21setExpectedFrameDeltaRKf");
    shx_get_expected_delta = (void *)so_symbol(&so_mod, "_Z21getExpectedFrameDeltav");
    of_get_frame_rate = (void *)so_symbol(&of_mod, "_Z14ofGetFrameRatev");
    of_get_target_frame_rate = (void *)so_symbol(&of_mod, "_Z20ofGetTargetFrameRatev");
    of_get_app_ptr_cached = (void *)so_symbol(&of_mod, "_Z11ofGetAppPtrv");
    shx_time_origin_us = (uint64_t)sceKernelGetSystemTimeWide();
    shx_last_frame_us = shx_time_origin_us;
    if (shx_set_expected_delta) {
        float expected = 1.0f;
        shx_set_expected_delta(&expected);
    }
}

static void force_game_frame_rate(int log_result) {
    void (*of_set_frame_rate)(int) = (void *)so_symbol(&of_mod, "_Z14ofSetFrameRatei");
    void (*of_set_time_mode_filtered)(float) = (void *)so_symbol(&of_mod, "_Z21ofSetTimeModeFilteredf");

    if (of_set_frame_rate) of_set_frame_rate(60);
    if (of_set_time_mode_filtered) of_set_time_mode_filtered(0.1f);
    if (shx_set_expected_delta) {
        float expected = 1.0f;
        shx_set_expected_delta(&expected);
    }

    void *app = of_get_app_ptr_cached ? of_get_app_ptr_cached() : NULL;
    if (app) {
        char *timing = (char *)app + 0x35ad8;
        int target = 60;
        double period = 1.0 / 60.0;
        *(int *)(timing + 0) = target;
        *(double *)(timing + 8) = period;
        *(int *)(timing + 16) = target;
        *(int *)(timing + 20) = target;
    }

    if (log_result) {
        char line[128];
        snprintf(line, sizeof(line), "force_fps ofSet=%d timeMode=%d app=%p direct=1\n",
                 of_set_frame_rate != NULL, of_set_time_mode_filtered != NULL, app);
        diag_append_main(line);
    }
}

static void update_game_timing(void) {
    uint64_t now_us = (uint64_t)sceKernelGetSystemTimeWide();
    uint64_t elapsed_ms = (now_us - shx_time_origin_us) / 1000;
    double frame_seconds = ((double)(now_us - shx_last_frame_us) / 1000000.0) * GAME_SPEED_SCALE;
    if (frame_seconds <= 0.0 || frame_seconds > 0.1) frame_seconds = 1.0 / 60.0;
    if (shx_set_elapsed_ms) shx_set_elapsed_ms(&elapsed_ms);
    if (shx_set_last_frame_ms) shx_set_last_frame_ms(&frame_seconds);
    shx_last_frame_us = now_us;

    if (++shx_force_fps_frames >= 60) {
        force_game_frame_rate(0);
        shx_force_fps_frames = 0;
    }

    if (++shx_timing_diag_frames == 300) {
        char line[256];
        float expected = shx_get_expected_delta ? shx_get_expected_delta() : -1.0f;
        float fps = of_get_frame_rate ? of_get_frame_rate() : -1.0f;
        float target = of_get_target_frame_rate ? of_get_target_frame_rate() : -1.0f;
        void *app = of_get_app_ptr_cached ? of_get_app_ptr_cached() : NULL;
        int app_target = app ? *(int *)((char *)app + 0x35ad8) : -1;
        double app_period = app ? *(double *)((char *)app + 0x35ad8 + 8) : -1.0;
        float game_time = -1.0f;
        float game_rate = -1.0f;
        double game_delta = -1.0;
        float intro_speed = -1.0f;
        if (shx_last_game_self) {
            char *timer = (char *)shx_last_game_self + 0x2958;
            game_time = *(float *)(timer + 4);
            game_rate = *(float *)(timer + 8);
            game_delta = *(double *)(timer + 64);
            intro_speed = *(float *)((char *)shx_last_game_self + 0x15c);
        }
        snprintf(line, sizeof(line), "timing_diag elapsed=%llu frame_s=%f expected=%f fps=%f target=%f app_target=%d app_period=%f scale=%f renders=%u updates=%u calcs=%u logic=%u game_time=%f game_rate=%f game_delta=%f intro=%f\n",
                 (unsigned long long)elapsed_ms, (float)frame_seconds, expected, fps, target, app_target, (float)app_period,
                 (float)GAME_SPEED_SCALE, shx_render_count, shx_update_count, shx_calculate_time_count, shx_gamelogic_count,
                 game_time, game_rate, (float)game_delta, intro_speed);
        diag_append_main(line);
        shx_render_count = 0;
        shx_update_count = 0;
        shx_calculate_time_count = 0;
        shx_gamelogic_count = 0;
        shx_timing_diag_frames = 0;
    }
}

int main(void) {
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

#ifdef USE_SCELIBC_IO
    fios_init(DATA_PATH);
#endif

    if (!module_loaded("kubridge")) fatal_error("kubridge.skprx is not installed.");
    if (!file_exists(DATA_PATH "libc++_shared.so")) fatal_error("Missing %slibc++_shared.so", DATA_PATH);
    if (!file_exists(DATA_PATH "liboboe.so")) fatal_error("Missing %sliboboe.so", DATA_PATH);
    if (!file_exists(DATA_PATH "libopenFrameworksAndroid.so")) fatal_error("Missing %slibopenFrameworksAndroid.so", DATA_PATH);
    if (!file_exists(DATA_PATH "libsuperhexagon.so")) fatal_error("Missing %slibsuperhexagon.so", DATA_PATH);

    settings_load();

    // so_util allocates modules at fixed addresses. Lower 0x90000000 ranges can
    // fail on Vita depending on the active memory layout; the original loader
    // convention starts in the 0x98000000 area.
    load_reloc_resolve(&libcxx_mod, DATA_PATH "libc++_shared.so", 0x98000000);
    load_reloc_resolve(&oboe_mod, DATA_PATH "liboboe.so", 0x99000000);
    load_reloc_resolve(&of_mod, DATA_PATH "libopenFrameworksAndroid.so", 0x9A000000);
    load_reloc_resolve(&so_mod, DATA_PATH "libsuperhexagon.so", 0x9C000000);

    uintptr_t initfileutils = so_symbol(&of_mod, "_ZN2of4priv13initfileutilsEv");
    if (initfileutils) hook_addr(initfileutils, (uintptr_t)&ret0);
    uintptr_t make_relative = so_symbol(&of_mod, "_ZN10ofFilePath12makeRelativeERKNSt6__ndk14__fs10filesystem4pathES5_");
    if (make_relative) hook_addr(make_relative, (uintptr_t)&empty_ndk_string_sret);

    native_text_install_hooks(&so_mod);
    native_audio_install_hooks(&so_mod);
    native_input_install_hooks(&so_mod);
    install_of_timing_hooks();
    install_timing_probe_hooks();
    // Keypoll confirm remains untouched; CROSS still flows through OF Android
    // events because forcing confirm state broke title->stage flow.
    so_patch();

    init_module(&libcxx_mod, "libc++_shared.so");
    init_module(&oboe_mod, "liboboe.so");
    init_module(&of_mod, "libopenFrameworksAndroid.so");
    init_module(&so_mod, "libsuperhexagon.so");

    gl_preload();
    jni_init();

    int (*of_jni_onload)(JavaVM *jvm, void *reserved) = (void *)so_symbol(&of_mod, "JNI_OnLoad");
    if (of_jni_onload) of_jni_onload(&jvm, NULL);

    gl_init();

    void (*of_set_data_dir)(JNIEnv *, jclass, jstring) = (void *)so_symbol(&of_mod, "Java_cc_openframeworks_OFAndroid_setAppDataDir");
    if (of_set_data_dir) {
        jstring data_dir = jni->NewStringUTF(&jni, "/ux0/data/superhexagon/assets");
        of_set_data_dir(&jni, NULL, data_dir);
    }

    call_void_jni(&so_mod, "Java_cc_openframeworks_OFAndroid_init");
    call_void_jni(&so_mod, "Java_cc_openframeworks_OFAndroid_onCreate");
    call_void_jni(&of_mod, "Java_cc_openframeworks_OFAndroid_onStart");
    call_void_jni(&of_mod, "Java_cc_openframeworks_OFAndroid_onResume");

    void (*of_set_asset)(JNIEnv *, jclass, jobject) = (void *)so_symbol(&of_mod, "Java_cc_openframeworks_OFAndroid_setAssetManager");
    if (of_set_asset) of_set_asset(&jni, NULL, (jobject)AAssetManager_create());

    void (*shx_set_asset)(JNIEnv *, jclass, jobject) = (void *)so_symbol(&so_mod, "Java_com_distractionware_superhexagon_OFActivity_setAssetManager");
    if (shx_set_asset) shx_set_asset(&jni, NULL, (jobject)AAssetManager_create());

    // Run surface creation now that the Java setupGL callback is stubbed.
    call_void_jni(&of_mod, "Java_cc_openframeworks_OFAndroid_onSurfaceCreated");
    call_setup(&of_mod);
    call_resize(&of_mod);
    init_game_timing();
    native_audio_start();
    force_game_frame_rate(1);

    uint32_t old_buttons = 0;
    int last_left_active = 0;
    int last_right_active = 0;
    int last_axis_dir = 0;
    int suppress_confirm_frames = 0;
    int last_gameplay_screen = 0;
    int dpad_locks_analog = 0;
    while (1) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t changed = pad.buttons ^ old_buttons;
        int text_screen = native_text_get_screen();
        int gameplay_screen = text_screen == 0 || text_screen == 3;
        int gameover_screen = text_screen == 4;
        int title_screen = text_screen == 1;
        int menu_screen = text_screen == 2;
        int credits_screen = text_screen == 5;
        int delete_confirm_screen = text_screen == 6;
        if (gameplay_screen && !last_gameplay_screen) dpad_locks_analog = 0;
        int dpad_left = !!(pad.buttons & SCE_CTRL_LEFT);
        int dpad_right = !!(pad.buttons & SCE_CTRL_RIGHT);
        if (gameplay_screen && !gameover_screen && (dpad_left || dpad_right)) dpad_locks_analog = 1;
        int analog_left = !dpad_locks_analog && pad.lx < 80;
        int analog_right = !dpad_locks_analog && pad.lx > 176;
        int left_input = dpad_left || analog_left;
        int right_input = dpad_right || analog_right;
        int left_active = !gameover_screen && (left_input || (gameplay_screen && (pad.buttons & SCE_CTRL_LTRIGGER)));
        int right_active = !gameover_screen && (right_input || (gameplay_screen && (pad.buttons & SCE_CTRL_RTRIGGER)));
        int menu_back_active = (gameover_screen || menu_screen || credits_screen) && !!(pad.buttons & SCE_CTRL_RTRIGGER);
        if (gameover_screen) {
            last_left_active = 0;
            last_right_active = 0;
            last_axis_dir = 0;
        } else {
            if (left_active != last_left_active) {
                send_key(&of_mod, left_active, 21, 0);
                send_key(&of_mod, left_active, 402, 0); // settings.dat left=p2
            }
            if (right_active != last_right_active) {
                send_key(&of_mod, right_active, 22, 0);
                send_key(&of_mod, right_active, 403, 0); // settings.dat right=p3
            }
        }
        int axis_dir = 0;
        if (left_active && !right_active) {
            axis_dir = -1;
        } else if (right_active && !left_active) {
            axis_dir = 1;
        }
        int confirm_active = !!(pad.buttons & SCE_CTRL_CROSS);
        int back_active = !!(pad.buttons & SCE_CTRL_CIRCLE) || menu_back_active;
        int start_active = !!(pad.buttons & SCE_CTRL_START);
        native_input_set_state(left_active && !right_active, right_active && !left_active, confirm_active, back_active, start_active);

        if (axis_dir != last_axis_dir) {
            float axis_value = (float)axis_dir;
            send_axis(&of_mod, 0, axis_value);  // AXIS_X
            send_axis(&of_mod, 15, axis_value); // AXIS_HAT_X
            last_axis_dir = axis_dir;
        }
        if ((changed & SCE_CTRL_CROSS) && suppress_confirm_frames <= 0) {
            int down = !!(pad.buttons & SCE_CTRL_CROSS);
            send_key(&of_mod, down, 23, '\n');
            send_touch(&of_mod, down, 103, GAME_VIEW_X(480.0f), GAME_VIEW_Y(360.0f));
        }
        if ((changed & SCE_CTRL_LTRIGGER) && (pad.buttons & SCE_CTRL_LTRIGGER) && (title_screen || menu_screen || credits_screen)) {
            send_tap(&of_mod, 104, GAME_VIEW_X(70.0f), GAME_VIEW_Y(32.0f));
            suppress_confirm_frames = 20;
        }
        if ((changed & SCE_CTRL_RTRIGGER) && (pad.buttons & SCE_CTRL_RTRIGGER) && title_screen) {
            send_tap(&of_mod, 105, GAME_VIEW_X(890.0f), GAME_VIEW_Y(32.0f));
            suppress_confirm_frames = 20;
        }
        if ((changed & SCE_CTRL_RTRIGGER) && (pad.buttons & SCE_CTRL_RTRIGGER) && delete_confirm_screen) {
            send_tap(&of_mod, 106, GAME_VIEW_X(890.0f), GAME_VIEW_Y(32.0f));
            send_key(&of_mod, 1, 4, 0);
            send_key(&of_mod, 1, 401, 0);
            send_key(&of_mod, 0, 4, 0);
            send_key(&of_mod, 0, 401, 0);
            suppress_confirm_frames = 20;
        }
        if (changed & SCE_CTRL_START) {
            int down = !!(pad.buttons & SCE_CTRL_START);
            send_key(&of_mod, down, 4, 0);
            send_key(&of_mod, down, 82, 0);  // Android MENU, used by some OF pause paths
            send_key(&of_mod, down, 108, 0); // Android BUTTON_START
        }
        if (changed & SCE_CTRL_CIRCLE) {
            int down = !!(pad.buttons & SCE_CTRL_CIRCLE);
            send_key(&of_mod, down, 4, 0);
            send_key(&of_mod, down, 401, 0); // settings.dat back=b1 / stage select
        }
        if ((changed & SCE_CTRL_RTRIGGER) && (gameover_screen || menu_screen || credits_screen)) {
            int down = !!(pad.buttons & SCE_CTRL_RTRIGGER);
            send_key(&of_mod, down, 4, 0);
            send_key(&of_mod, down, 401, 0);
            if (down) suppress_confirm_frames = 20;
        }
        if (suppress_confirm_frames > 0) suppress_confirm_frames--;
        old_buttons = pad.buttons;
        last_left_active = left_active;
        last_right_active = right_active;
        last_gameplay_screen = gameplay_screen;

        native_text_begin_frame();
        update_game_timing();
        shx_render_count++;
        call_render(&of_mod);
        native_text_render_overlay();
        gl_swap();
    }

    sceKernelExitProcess(0);
    return 0;
}

void controls_handler_key(int32_t keycode, ControlsAction action) { (void)keycode; (void)action; }
void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) { (void)id; (void)x; (void)y; (void)action; }
void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) { (void)which; (void)x; (void)y; (void)action; }

#endif
