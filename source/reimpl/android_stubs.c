#include "reimpl/android_stubs.h"

#include <errno.h>
#include <malloc.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

const char *AMEDIAFORMAT_KEY_MIME = "mime";
const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT = "channel-count";
const char *AMEDIAFORMAT_KEY_SAMPLE_RATE = "sample-rate";

void *__memcpy_chk(void *dst, const void *src, size_t len, size_t dst_len) {
    (void)dst_len;
    return memcpy(dst, src, len);
}

size_t __strlen_chk(const char *s, size_t max_len) {
    (void)max_len;
    return strlen(s);
}

int __vsnprintf_chk(char *s, size_t max_len, int flags, size_t slen, const char *fmt, va_list ap) {
    (void)flags;
    (void)slen;
    return vsnprintf(s, max_len, fmt, ap);
}

int __cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso_symbol) {
    (void)dtor;
    (void)obj;
    (void)dso_symbol;
    return 0;
}

int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request, struct timespec *remain) {
    (void)clock_id;
    if (flags != 0) {
        errno = ENOTSUP;
        return ENOTSUP;
    }
    return nanosleep(request, remain);
}

int sched_getscheduler(int pid) {
    (void)pid;
    return SCHED_OTHER;
}

char *getcwd_soloader(char *buf, size_t size) {
    const char *cwd = "/ux0/data/superhexagon/assets";
    size_t need = strlen(cwd) + 1;
    if (!buf) return strdup(cwd);
    if (size < need) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buf, cwd, need);
    return buf;
}

long pathconf_soloader(const char *path, int name) {
    (void)path;
    // _PC_PATH_MAX / _PC_NAME_MAX are the only values libc++ filesystem tends
    // to query during startup. Return conservative positive limits so it does
    // not throw filesystem_error while probing Vita paths.
    if (name == 4) return 1024;   // _PC_PATH_MAX on newlib
    if (name == 3) return 255;    // _PC_NAME_MAX on newlib
    return 1024;
}

int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags) {
    (void)dirfd; (void)pathname; (void)mode; (void)flags;
    errno = ENOTSUP;
    return -1;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) return EINVAL;
    void *p = memalign(alignment, size);
    if (!p) return ENOMEM;
    *memptr = p;
    return 0;
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    (void)pathname; (void)buf; (void)bufsiz;
    errno = EINVAL;
    return -1;
}

ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    (void)out_fd; (void)in_fd; (void)offset; (void)count;
    errno = ENOTSUP;
    return -1;
}

int symlink(const char *target, const char *linkpath) {
    (void)target; (void)linkpath;
    errno = ENOTSUP;
    return -1;
}

int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags) {
    (void)dirfd; (void)pathname; (void)times; (void)flags;
    errno = ENOTSUP;
    return -1;
}

struct passwd *getpwuid(uid_t uid) {
    static struct passwd pw;
    static char name[] = "vita";
    static char dir[] = "ux0:data";
    static char shell[] = "";
    pw.pw_name = name;
    pw.pw_uid = uid;
    pw.pw_gid = 0;
    pw.pw_dir = dir;
    pw.pw_shell = shell;
    return &pw;
}

FILE *popen(const char *command, const char *type) {
    (void)command; (void)type;
    errno = ENOTSUP;
    return NULL;
}

int pclose(FILE *stream) {
    (void)stream;
    errno = ECHILD;
    return -1;
}

static int valid_ptr(const void *p) {
    uintptr_t v = (uintptr_t)p;
    return (v >= 0x81000000u && v < 0xA2000000u);
}

static const char *typeinfo_name(const void *ti) {
    if (!valid_ptr(ti)) return NULL;
    const uintptr_t *words = (const uintptr_t *)ti;
    if (!valid_ptr((const void *)words[1])) return NULL;
    return (const char *)words[1];
}

static int name_has(const char *name, const char *needle) {
    return name && strstr(name, needle) != NULL;
}

void empty_ndk_string_sret(void *ret, const void *a, const void *b) {
    (void)a;
    (void)b;
    memset(ret, 0, 12);
}

void *__dynamic_cast_soloader(void *sub, const void *src, const void *dst, ptrdiff_t src2dst) {
    (void)src2dst;
    if (!sub || !valid_ptr(sub)) return NULL;

    const char *src_name = typeinfo_name(src);
    const char *dst_name = typeinfo_name(dst);
    const char *dyn_name = NULL;

    uintptr_t vtable = 0;
    if (valid_ptr(*(void **)sub)) {
        vtable = *(uintptr_t *)sub;
        if (valid_ptr((void *)(vtable - 4))) {
            uintptr_t dyn_ti = *(uintptr_t *)(vtable - 4);
            dyn_name = typeinfo_name((void *)dyn_ti);
        }
    }

    if (src_name && dst_name && strcmp(src_name, dst_name) == 0) return sub;
    if (dyn_name && dst_name && strcmp(dyn_name, dst_name) == 0) return sub;

    if (name_has(dyn_name, "ofAppAndroidWindow") &&
        (name_has(dst_name, "ofAppAndroidWindow") || name_has(dst_name, "ofAppBaseWindow") || name_has(dst_name, "ofBaseGL"))) {
        return sub;
    }

    if ((name_has(dyn_name, "superhex") || name_has(dyn_name, "ofxAppAndroidLayer")) &&
        (name_has(dst_name, "ofBaseApp") || name_has(dst_name, "ofxAndroidApp") || name_has(dst_name, "superhex"))) {
        return sub;
    }

    return NULL;
}

void *AMediaExtractor_new(void) { return calloc(1, 4); }
void AMediaExtractor_delete(void *extractor) { free(extractor); }
int AMediaExtractor_setDataSourceFd(void *extractor, int fd, long offset, long length) {
    (void)extractor; (void)fd; (void)offset; (void)length;
    return -1;
}
int AMediaExtractor_getTrackFormat(void *extractor, size_t idx) { (void)extractor; (void)idx; return 0; }
int AMediaExtractor_selectTrack(void *extractor, size_t idx) { (void)extractor; (void)idx; return -1; }
ssize_t AMediaExtractor_readSampleData(void *extractor, uint8_t *buffer, size_t capacity) {
    (void)extractor; (void)buffer; (void)capacity;
    return -1;
}
long AMediaExtractor_getSampleTime(void *extractor) { (void)extractor; return -1; }
int AMediaExtractor_advance(void *extractor) { (void)extractor; return 0; }

void *AMediaCodec_createDecoderByType(const char *mime_type) { (void)mime_type; return NULL; }
int AMediaCodec_configure(void *codec, void *format, void *surface, void *crypto, uint32_t flags) {
    (void)codec; (void)format; (void)surface; (void)crypto; (void)flags;
    return -1;
}
int AMediaCodec_start(void *codec) { (void)codec; return -1; }
void AMediaCodec_delete(void *codec) { (void)codec; }
ssize_t AMediaCodec_dequeueInputBuffer(void *codec, int64_t timeout_us) { (void)codec; (void)timeout_us; return -1; }
uint8_t *AMediaCodec_getInputBuffer(void *codec, size_t idx, size_t *out_size) {
    (void)codec; (void)idx;
    if (out_size) *out_size = 0;
    return NULL;
}
int AMediaCodec_queueInputBuffer(void *codec, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags) {
    (void)codec; (void)idx; (void)offset; (void)size; (void)time; (void)flags;
    return -1;
}
ssize_t AMediaCodec_dequeueOutputBuffer(void *codec, void *info, int64_t timeout_us) {
    (void)codec; (void)info; (void)timeout_us;
    return -1;
}
uint8_t *AMediaCodec_getOutputBuffer(void *codec, size_t idx, size_t *out_size) {
    (void)codec; (void)idx;
    if (out_size) *out_size = 0;
    return NULL;
}
void AMediaCodec_releaseOutputBuffer(void *codec, size_t idx, int render) { (void)codec; (void)idx; (void)render; }
void *AMediaCodec_getOutputFormat(void *codec) { (void)codec; return NULL; }

void AMediaFormat_delete(void *format) { (void)format; }
int AMediaFormat_getInt32(void *format, const char *name, int32_t *out) {
    (void)format; (void)name;
    if (out) *out = 0;
    return 0;
}
int AMediaFormat_getString(void *format, const char *name, const char **out) {
    (void)format; (void)name;
    if (out) *out = "";
    return 0;
}
const char *AMediaFormat_toString(void *format) { (void)format; return "AMediaFormat(stub)"; }
