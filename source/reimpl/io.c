/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

static const char *path_basename(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static int is_game_save_name(const char *name) {
    return !strcmp(name, "settings.dat") || !strcmp(name, "scores.dat") || !strcmp(name, "suphex.dat");
}

static int has_assets_component(const char *path) {
    return !strncmp(path, "assets/", 7) || strstr(path, "/assets/") || strstr(path, ":assets/");
}

static int mode_writes_file(const char *mode) {
    return mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
}

static int bionic_flags_write_file(int oflag) {
    return (oflag & BIONIC_O_WRONLY) || (oflag & BIONIC_O_RDWR) ||
           (oflag & BIONIC_O_CREAT) || (oflag & BIONIC_O_TRUNC) ||
           (oflag & BIONIC_O_APPEND);
}

static const char *translate_save_path(const char *path, char *buf, size_t buf_size, int writing) {
    if (!path) return path;
    const char *base = path_basename(path);
    if (!is_game_save_name(base)) return path;
    if (!writing && has_assets_component(path)) return path;
    snprintf(buf, buf_size, DATA_PATH "%s", base);
    return buf;
}

static const char *translate_path(const char *path, char *buf, size_t buf_size) {
    if (!path) return path;

    // openFrameworks sometimes prepends its data root to paths that are
    // already absolute in Vita form, yielding e.g.
    //   ux0:/data/superhexagon/assets/ux0:/data/superhexagon/assets/foo.png
    // Collapse those back to the embedded absolute Vita path before calling
    // newlib/Vita filesystem APIs.
    const char *embedded_ux0 = strstr(path + 1, "ux0:/");
    if (embedded_ux0) path = embedded_ux0;
    const char *embedded_uma0 = strstr(path + 1, "uma0:/");
    if (embedded_uma0) path = embedded_uma0;

    const char *rest = NULL;
    const char *prefix = NULL;

    if (strncmp(path, "app0:/ux0/", 10) == 0) {
        prefix = "ux0:/";
        rest = path + 10;
    } else if (strncmp(path, "/ux0/", 5) == 0) {
        prefix = "ux0:/";
        rest = path + 5;
    } else if (strncmp(path, "app0:/uma0/", 11) == 0) {
        prefix = "uma0:/";
        rest = path + 11;
    } else if (strncmp(path, "/uma0/", 6) == 0) {
        prefix = "uma0:/";
        rest = path + 6;
    }

    if (!prefix) return path;

    snprintf(buf, buf_size, "%s%s", prefix, rest);
    return buf;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    char asset_fallback_buf[PATH_MAX];
    int writing = mode_writes_file(mode);
    filename = translate_path(filename, path_buf, sizeof(path_buf));
    filename = translate_save_path(filename, save_path_buf, sizeof(save_path_buf), writing);

    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    if (!ret && !writing && !strcmp(path_basename(filename), "settings.dat")) {
        snprintf(asset_fallback_buf, sizeof(asset_fallback_buf), DATA_PATH "assets/settings.dat");
#ifdef USE_SCELIBC_IO
        ret = sceLibcBridge_fopen(asset_fallback_buf, mode);
#else
        ret = fopen(asset_fallback_buf, mode);
#endif
        if (ret)
            l_debug("fopen fallback(%s, %s): %p", asset_fallback_buf, mode, ret);
    }

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    char asset_fallback_buf[PATH_MAX];
    int writing = bionic_flags_write_file(oflag);
    path = translate_path(path, path_buf, sizeof(path_buf));
    path = translate_save_path(path, save_path_buf, sizeof(save_path_buf), writing);

    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    } else if (strcmp(path, "/dev/urandom") == 0) {
        return open_soloader("app0:/urandom", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    if (ret < 0 && !writing && !strcmp(path_basename(path), "settings.dat")) {
        snprintf(asset_fallback_buf, sizeof(asset_fallback_buf), DATA_PATH "assets/settings.dat");
        ret = open(asset_fallback_buf, oflag, mode);
        if (ret >= 0) l_debug("open fallback(%s, %x): %i", asset_fallback_buf, oflag, ret);
    }
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    char asset_fallback_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    path = translate_save_path(path, save_path_buf, sizeof(save_path_buf), 0);

    if (strcmp(path, "/system/lib/libOpenSLES.so") == 0) {
        l_debug("stat(%s): returning 0 in case this is a check for OpenSLES support", path);
        return 0;
    }

    struct stat st;
    int res = stat(path, &st);
    if (res != 0 && !strcmp(path_basename(path), "settings.dat")) {
        snprintf(asset_fallback_buf, sizeof(asset_fallback_buf), DATA_PATH "assets/settings.dat");
        res = stat(asset_fallback_buf, &st);
        if (res == 0) path = asset_fallback_buf;
    }

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int lstat_soloader(const char * path, stat64_bionic * buf) {
    return stat_soloader(path, buf);
}

int access_soloader(const char *path, int amode) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    char asset_fallback_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    path = translate_save_path(path, save_path_buf, sizeof(save_path_buf), !!(amode & W_OK));
    int ret = access(path, amode);
    if (ret != 0 && !(amode & W_OK) && !strcmp(path_basename(path), "settings.dat")) {
        snprintf(asset_fallback_buf, sizeof(asset_fallback_buf), DATA_PATH "assets/settings.dat");
        ret = access(asset_fallback_buf, amode);
        if (ret == 0) path = asset_fallback_buf;
    }
    l_debug("access(%s, %x): %i", path, amode, ret);
    return ret;
}

int chdir_soloader(const char *path) {
    char path_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    int ret = chdir(path);
    l_debug("chdir(%s): %i", path, ret);
    return ret;
}

int mkdir_soloader(const char *path, mode_t mode) {
    char path_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    int ret = mkdir(path, mode);
    l_debug("mkdir(%s, %o): %i", path, mode, ret);
    return ret;
}

char *realpath_soloader(const char *path, char *resolved) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    path = translate_save_path(path, save_path_buf, sizeof(save_path_buf), 0);

    char *ret = realpath(path, resolved);
    if (!ret) {
        struct stat st;
        if (stat(path, &st) == 0) {
            if (!resolved) {
                resolved = malloc(PATH_MAX);
                if (!resolved) {
                    errno = ENOMEM;
                    return NULL;
                }
            }
            strncpy(resolved, path, PATH_MAX - 1);
            resolved[PATH_MAX - 1] = '\0';
            ret = resolved;
        }
    }

    l_debug("realpath(%s): %p", path, ret);
    return ret;
}

ssize_t readlink_soloader(const char *path, char *buf, size_t bufsiz) {
    (void)buf;
    (void)bufsiz;
    char path_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    l_debug("readlink(%s): not a symlink", path);
    errno = EINVAL;
    return -1;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    char path_buf[PATH_MAX];
    const char *pathname = translate_path(_pathname, path_buf, sizeof(path_buf));
    DIR* ret = opendir(pathname);
    l_debug("opendir(\"%s\"): %p", pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int remove_soloader(const char *path) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    path = translate_save_path(path, save_path_buf, sizeof(save_path_buf), 1);
    int ret = remove(path);
    l_debug("remove(%s): %i", path, ret);
    return ret;
}

int rename_soloader(const char *oldpath, const char *newpath) {
    char old_path_buf[PATH_MAX];
    char new_path_buf[PATH_MAX];
    char old_save_path_buf[PATH_MAX];
    char new_save_path_buf[PATH_MAX];
    oldpath = translate_path(oldpath, old_path_buf, sizeof(old_path_buf));
    newpath = translate_path(newpath, new_path_buf, sizeof(new_path_buf));
    oldpath = translate_save_path(oldpath, old_save_path_buf, sizeof(old_save_path_buf), 1);
    newpath = translate_save_path(newpath, new_save_path_buf, sizeof(new_save_path_buf), 1);
    int ret = rename(oldpath, newpath);
    l_debug("rename(%s, %s): %i", oldpath, newpath, ret);
    return ret;
}

int unlink_soloader(const char *path) {
    char path_buf[PATH_MAX];
    char save_path_buf[PATH_MAX];
    path = translate_path(path, path_buf, sizeof(path_buf));
    path = translate_save_path(path, save_path_buf, sizeof(save_path_buf), 1);
    int ret = unlink(path);
    l_debug("unlink(%s): %i", path, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
