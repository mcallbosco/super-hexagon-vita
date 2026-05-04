#ifndef SHX_ANDROID_STUBS_H
#define SHX_ANDROID_STUBS_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *AMEDIAFORMAT_KEY_MIME;
extern const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT;
extern const char *AMEDIAFORMAT_KEY_SAMPLE_RATE;

void *__memcpy_chk(void *dst, const void *src, size_t len, size_t dst_len);
size_t __strlen_chk(const char *s, size_t max_len);
int __vsnprintf_chk(char *s, size_t max_len, int flags, size_t slen, const char *fmt, va_list ap);
int __cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso_symbol);
int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request, struct timespec *remain);
int sched_getscheduler(int pid);
char *getcwd_soloader(char *buf, size_t size);
long pathconf_soloader(const char *path, int name);
int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int posix_memalign(void **memptr, size_t alignment, size_t size);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
int symlink(const char *target, const char *linkpath);
int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);
struct passwd *getpwuid(uid_t uid);
FILE *popen(const char *command, const char *type);
int pclose(FILE *stream);
void *__dynamic_cast_soloader(void *sub, const void *src, const void *dst, ptrdiff_t src2dst);
void empty_ndk_string_sret(void *ret, const void *a, const void *b);

void *AMediaExtractor_new(void);
void AMediaExtractor_delete(void *extractor);
int AMediaExtractor_setDataSourceFd(void *extractor, int fd, long offset, long length);
int AMediaExtractor_getTrackFormat(void *extractor, size_t idx);
int AMediaExtractor_selectTrack(void *extractor, size_t idx);
ssize_t AMediaExtractor_readSampleData(void *extractor, uint8_t *buffer, size_t capacity);
long AMediaExtractor_getSampleTime(void *extractor);
int AMediaExtractor_advance(void *extractor);

void *AMediaCodec_createDecoderByType(const char *mime_type);
int AMediaCodec_configure(void *codec, void *format, void *surface, void *crypto, uint32_t flags);
int AMediaCodec_start(void *codec);
void AMediaCodec_delete(void *codec);
ssize_t AMediaCodec_dequeueInputBuffer(void *codec, int64_t timeout_us);
uint8_t *AMediaCodec_getInputBuffer(void *codec, size_t idx, size_t *out_size);
int AMediaCodec_queueInputBuffer(void *codec, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags);
ssize_t AMediaCodec_dequeueOutputBuffer(void *codec, void *info, int64_t timeout_us);
uint8_t *AMediaCodec_getOutputBuffer(void *codec, size_t idx, size_t *out_size);
void AMediaCodec_releaseOutputBuffer(void *codec, size_t idx, int render);
void *AMediaCodec_getOutputFormat(void *codec);

void AMediaFormat_delete(void *format);
int AMediaFormat_getInt32(void *format, const char *name, int32_t *out);
int AMediaFormat_getString(void *format, const char *name, const char **out);
const char *AMediaFormat_toString(void *format);

#ifdef __cplusplus
}
#endif

#endif
