/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  glutil.h
 * @brief OpenGL API initializer, related functions.
 */

#ifndef SOLOADER_GLUTIL_H
#define SOLOADER_GLUTIL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_init();

void gl_preload();

void gl_swap();

GLuint glCreateShader_soloader(GLenum type);

void glCompileShader_soloader(GLuint shader);

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length);

void glBindAttribLocation_soloader(GLuint program, GLuint index, const GLchar *name);
void glLinkProgram_soloader(GLuint program);
void glGetProgramiv_soloader(GLuint program, GLenum pname, GLint *params);
void glUseProgram_soloader(GLuint program);
GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name);
GLint glGetAttribLocation_soloader(GLuint program, const GLchar *name);
void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count);
void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
int glutil_draw_seen(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GLUTIL_H
