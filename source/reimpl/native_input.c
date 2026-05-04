#include "reimpl/native_input.h"

#include "so_util/so_util.h"
#include "utils/logger.h"

#include <psp2/io/fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DIAG_PATH DATA_PATH "native_diag.txt"

static volatile int g_left = 0;
static volatile int g_right = 0;
static volatile int g_confirm = 0;
static volatile int g_back = 0;
static volatile int g_start = 0;
static int g_seen_keys[256];
static int g_seen_count = 0;

static void diag_append(const char *msg) {
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

void native_input_set_state(int left, int right, int confirm, int back, int start) {
    g_left = left;
    g_right = right;
    g_confirm = confirm;
    g_back = back;
    g_start = start;
}

static int key_matches(int key, int low, int android, int queued) {
    return key == low || key == android || key == queued;
}

static int forced_key_state(int key) {
    // Assets/settings.dat maps left=p2, right=p3, confirm=b0, back=b1.
    // Runtime diagnostics showed the game actually polls p2/p3 as 102/103,
    // plus Android DPAD left/right as 21/22 in some states. Accept raw,
    // Android, observed, and queued (+500) variants.
    if (g_left && (key_matches(key, 2, 402, 902) || key == 21 || key == 102 || key == 602)) return 1;
    if (g_right && (key_matches(key, 3, 403, 903) || key == 22 || key == 103 || key == 603)) return 1;
    if (g_back && (key_matches(key, 1, 401, 901) || key == 4 || key == 101 || key == 601)) return 1;
    if (g_start && (key_matches(key, 1, 401, 901) || key == 4 || key == 101 || key == 601 ||
                    key == 108 || key == 608 || key == 1008 || key == 82 || key == 582 || key == 982)) return 1;
    // Do not force confirm here: the normal OF key/touch path already handles
    // CROSS, and forcing keypoll confirm interfered with title->stage flow.
    (void)g_confirm;
    return 0;
}

static void log_key_once(const char *fn, int key) {
    for (int i = 0; i < g_seen_count; ++i) if (g_seen_keys[i] == key) return;
    if (g_seen_count < (int)(sizeof(g_seen_keys) / sizeof(g_seen_keys[0]))) {
        g_seen_keys[g_seen_count++] = key;
        char line[96];
        snprintf(line, sizeof(line), "input %s key=%d\n", fn, key);
        diag_append(line);
    }
}

int native_keypoll_isDown(void *self, int key) {
    log_key_once("isDown", key);
    if (forced_key_state(key)) return 1;
    if (key < 0 || key > 1999) return 0;
    return *((volatile uint8_t *)self + key + 0x23c) != 0;
}

int native_keypoll_isUp(void *self, int key) {
    log_key_once("isUp", key);
    return native_keypoll_isDown(self, key) ? 0 : 1;
}

int native_keypoll_isDownReleased(void *self, int key) {
    log_key_once("isDownReleased", key);
    if (key < 0 || key > 1999) return 0;
    return *((volatile uint8_t *)self + key + 0xa0c) != 0;
}

int native_keypoll_gettouchx(void *self) {
    if (g_left) return 120;
    if (g_right) return 840;
    (void)self;
    return 480;
}

int native_keypoll_gettouchy(void *self) {
    if (g_left || g_right) return 420;
    (void)self;
    return 360;
}

void native_input_install_hooks(void *superhex_mod_ptr) {
    so_module *mod = (so_module *)superhex_mod_ptr;
    uintptr_t a;
    if ((a = so_symbol(mod, "_ZN12keypollclass6isDownEi"))) hook_addr(a, (uintptr_t)&native_keypoll_isDown);
    // Keep the rest of keypollclass untouched; hooking release/touch helpers
    // interfered with the title-screen confirm flow. Left/right only need the
    // isDown override for the configured p2/p3 keys.
    l_info("native input: installed keypoll isDown hook");
}
