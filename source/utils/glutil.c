/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>

// Helpers for our handling of shaders
GLboolean skip_next_compile = GL_FALSE;
char next_shader_fname[256];
static volatile int draw_seen = 0;
static GLenum shader_types[256];
static int force_simple_shaders = 1;
void load_shader(GLuint shader, const char * string, size_t length);

void gl_preload() {
    if (!file_exists("ur0:/data/libshacccg.suprx")
        && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Google \"ShaRKBR33D\" for quick installation.");
    }

#ifdef USE_GLSL_SHADERS
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
#endif
}

void gl_init() {
    vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
}

void gl_swap() {
    vglSwapBuffers(GL_FALSE);
}

static char *replace_all_owned(const char *src, const char *needle, const char *replacement) {
    size_t src_len = strlen(src);
    size_t needle_len = strlen(needle);
    size_t repl_len = strlen(replacement);
    size_t count = 0;
    const char *p = src;
    while ((p = strstr(p, needle))) {
        count++;
        p += needle_len;
    }
    if (!count) return strdup(src);

    size_t out_len = src_len + 1;
    if (repl_len >= needle_len) out_len += count * (repl_len - needle_len);
    else out_len -= count * (needle_len - repl_len);
    char *out = malloc(out_len);
    if (!out) return strdup(src);

    char *dst = out;
    p = src;
    const char *next;
    while ((next = strstr(p, needle))) {
        size_t chunk = (size_t)(next - p);
        memcpy(dst, p, chunk);
        dst += chunk;
        memcpy(dst, replacement, repl_len);
        dst += repl_len;
        p = next + needle_len;
    }
    strcpy(dst, p);
    return out;
}

static char *sanitize_glsl_owned(const char *src) {
    char *a = replace_all_owned(src, "#extension GL_OES_EGL_image_external : require", "// GL_OES_EGL_image_external removed for VitaGL");
    char *b = replace_all_owned(a, "samplerExternalOES", "sampler2D");
    free(a);
    return b;
}

GLuint glCreateShader_soloader(GLenum type) {
    GLuint shader = glCreateShader(type);
    if (shader < (sizeof(shader_types) / sizeof(shader_types[0]))) shader_types[shader] = type;
    return shader;
}

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glShaderSource<%p>(shader: %i, count: %i, string: %p, length: %p)\n", __builtin_return_address(0), shader, count, string, _length);
#endif
    if (!string) {
        l_error("<%p> Shader source string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    } else if (!*string) {
        l_error("<%p> Shader source *string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    }

    size_t total_length = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            total_length += strlen(string[i]);
        } else {
            total_length += _length[i];
        }
    }

    char * str = malloc(total_length+1);
    size_t l = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            memcpy(str + l, string[i], strlen(string[i]));
            l += strlen(string[i]);
        } else {
            memcpy(str + l, string[i], _length[i]);
            l += _length[i];
        }
    }
    str[total_length] = '\0';

    char *sanitized = sanitize_glsl_owned(str);
    if (force_simple_shaders && shader < (sizeof(shader_types) / sizeof(shader_types[0]))) {
        if (shader_types[shader] == GL_VERTEX_SHADER) {
            const char *simple_vs = "attribute vec4 position; attribute vec4 color; uniform mat4 modelViewProjectionMatrix; varying vec4 colorVarying; void main(){ colorVarying = color; gl_Position = modelViewProjectionMatrix * position; }";
            load_shader(shader, simple_vs, strlen(simple_vs));
        } else if (shader_types[shader] == GL_FRAGMENT_SHADER) {
            const char *simple_fs = "precision mediump float; varying vec4 colorVarying; void main(){ gl_FragColor = colorVarying; }";
            load_shader(shader, simple_fs, strlen(simple_fs));
        } else {
            load_shader(shader, sanitized, strlen(sanitized));
        }
    } else {
        load_shader(shader, sanitized, strlen(sanitized));
    }

    free(sanitized);
    free(str);
}

typedef struct PendingAttribBind {
    GLuint program;
    GLuint index;
    char name[64];
} PendingAttribBind;

static PendingAttribBind pending_attrib_binds[128];
static int pending_attrib_bind_count = 0;

void glBindAttribLocation_soloader(GLuint program, GLuint index, const GLchar *name) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glBindAttribLocation<%p>(program: %u, index: %u, name: %s)\n", __builtin_return_address(0), program, index, name ? name : "(null)");
#endif
    // Android/openFrameworks calls this before linking. vitaGL's implementation
    // dereferences the translated GXM program immediately, which is still NULL
    // before glLinkProgram. Defer and replay after linking instead.
    if (!program || !name || pending_attrib_bind_count >= (int)(sizeof(pending_attrib_binds) / sizeof(pending_attrib_binds[0]))) return;
    PendingAttribBind *b = &pending_attrib_binds[pending_attrib_bind_count++];
    b->program = program;
    b->index = index;
    strncpy(b->name, name, sizeof(b->name) - 1);
    b->name[sizeof(b->name) - 1] = '\0';
}

void glLinkProgram_soloader(GLuint program) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glLinkProgram<%p>(program: %u)\n", __builtin_return_address(0), program);
#endif
    glLinkProgram(program);
    for (int i = 0; i < pending_attrib_bind_count; ++i) {
        PendingAttribBind *b = &pending_attrib_binds[i];
        if (b->program == program) glBindAttribLocation(program, b->index, b->name);
    }
}

void glGetProgramiv_soloader(GLuint program, GLenum pname, GLint *params) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glGetProgramiv<%p>(program: %u, pname: 0x%x)\n", __builtin_return_address(0), program, pname);
#endif
    if (pname == GL_LINK_STATUS || pname == GL_VALIDATE_STATUS) {
        *params = GL_TRUE;
        return;
    }
    glGetProgramiv(program, pname, params);
}

void glUseProgram_soloader(GLuint program) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glUseProgram<%p>(program: %u)\n", __builtin_return_address(0), program);
#endif
    glUseProgram(program);
}

GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glGetUniformLocation<%p>(program: %u, name: %s)\n", __builtin_return_address(0), program, name ? name : "(null)");
#endif
    return glGetUniformLocation(program, name);
}

int glutil_draw_seen(void) {
    return draw_seen;
}

void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count) {
    draw_seen = 1;
    glDrawArrays(mode, first, count);
}

void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    draw_seen = 1;
    glDrawElements(mode, count, type, indices);
}

GLint glGetAttribLocation_soloader(GLuint program, const GLchar *name) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glGetAttribLocation<%p>(program: %u, name: %s)\n", __builtin_return_address(0), program, name ? name : "(null)");
#endif
    return glGetAttribLocation(program, name);
}

void glCompileShader_soloader(GLuint shader) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glCompileShader<%p>(shader: %i)\n", __builtin_return_address(0), shader);
#endif

#ifndef USE_GXP_SHADERS
    if (!skip_next_compile) {
        glCompileShader(shader);
#ifdef DUMP_COMPILED_SHADERS
        void *bin = vglMalloc(32 * 1024);
        GLsizei len;
        vglGetShaderBinary(shader, 32 * 1024, &len, bin);
        file_save(next_shader_fname, bin, len);
        vglFree(bin);
#endif
    }
    skip_next_compile = GL_FALSE;
#endif
}

#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else {
        glShaderSource(shader, 1, &string, &length);
        strcpy(next_shader_fname, gxp_path);
    }

    free(sha_name);
}
#elif defined(USE_GLSL_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    glShaderSource(shader, 1, &string, &length);
}
#elif defined(USE_CG_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    char cg_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);
    snprintf(cg_path, sizeof(cg_path), DATA_PATH"cg/%s.cg", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else if (file_exists(cg_path)) {
        char *buffer;
        size_t size;

        file_load(cg_path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);
        strcpy(next_shader_fname, gxp_path);

        free(buffer);
        skip_next_compile = GL_FALSE;
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }

        skip_next_compile = GL_FALSE;
    }

    free(sha_name);
}
#elif defined(USE_CG_SHADERS) || defined(USE_GXP_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char path[256];
#ifdef USE_CG_SHADERS
    snprintf(path, sizeof(path), DATA_PATH"cg/%s.cg", sha_name);
#else
    snprintf(path, sizeof(path), DATA_PATH"gxp/%s.gxp", sha_name);
#endif

    if (file_exists(path)) {
#ifdef USE_CG_SHADERS
        char *buffer;
        size_t size;

        file_load(path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);

        free(buffer);
#else
        uint8_t *buffer;
        size_t size;

        file_load(path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
#endif
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }
    }

    free(sha_name);
}
#else
#error "Define one of (USE_GLSL_SHADERS, USE_CG_SHADERS, USE_GXP_SHADERS)"
#endif
