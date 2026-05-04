#ifndef SHX_NATIVE_TEXT_H
#define SHX_NATIVE_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void native_text_install_hooks(void *superhex_mod);
void native_text_begin_frame(void);
void native_text_render_overlay(void);
int native_text_get_screen(void);
void native_graphics_print(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered);
void native_graphics_rprint(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered);
void native_graphics_bigprint(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered, int size);

#ifdef __cplusplus
}
#endif

#endif
