#include "reimpl/native_text.h"

#include "so_util/so_util.h"
#include "utils/logger.h"

#include <kubridge.h>
#include <vitaGL.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#define DIAG_PATH DATA_PATH "native_diag.txt"
#define FONT_PATH DATA_PATH "assets/bumpitup.ttf"
#define FONT_FIRST_CHAR 32
#define FONT_CHAR_COUNT 95
#define FONT_ATLAS_W 1024
#define FONT_ATLAS_H 1024

static void text_diag_append(const char *msg) {
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

static const uint8_t *glyph5x7(char c) {
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t box[7] = {31,17,17,17,17,17,31};
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case ' ': return blank; case '!': { static const uint8_t g[7]={4,4,4,4,4,0,4}; return g; }
        case '.': { static const uint8_t g[7]={0,0,0,0,0,6,6}; return g; }
        case ',': { static const uint8_t g[7]={0,0,0,0,0,6,4}; return g; }
        case ':': { static const uint8_t g[7]={0,6,6,0,6,6,0}; return g; }
        case '-': { static const uint8_t g[7]={0,0,0,31,0,0,0}; return g; }
        case '+': { static const uint8_t g[7]={0,4,4,31,4,4,0}; return g; }
        case '/': { static const uint8_t g[7]={1,2,2,4,8,8,16}; return g; }
        case '0': { static const uint8_t g[7]={14,17,19,21,25,17,14}; return g; }
        case '1': { static const uint8_t g[7]={4,12,4,4,4,4,14}; return g; }
        case '2': { static const uint8_t g[7]={14,17,1,2,4,8,31}; return g; }
        case '3': { static const uint8_t g[7]={30,1,1,14,1,1,30}; return g; }
        case '4': { static const uint8_t g[7]={2,6,10,18,31,2,2}; return g; }
        case '5': { static const uint8_t g[7]={31,16,16,30,1,1,30}; return g; }
        case '6': { static const uint8_t g[7]={14,16,16,30,17,17,14}; return g; }
        case '7': { static const uint8_t g[7]={31,1,2,4,8,8,8}; return g; }
        case '8': { static const uint8_t g[7]={14,17,17,14,17,17,14}; return g; }
        case '9': { static const uint8_t g[7]={14,17,17,15,1,1,14}; return g; }
        case 'A': { static const uint8_t g[7]={14,17,17,31,17,17,17}; return g; }
        case 'B': { static const uint8_t g[7]={30,17,17,30,17,17,30}; return g; }
        case 'C': { static const uint8_t g[7]={14,17,16,16,16,17,14}; return g; }
        case 'D': { static const uint8_t g[7]={30,17,17,17,17,17,30}; return g; }
        case 'E': { static const uint8_t g[7]={31,16,16,30,16,16,31}; return g; }
        case 'F': { static const uint8_t g[7]={31,16,16,30,16,16,16}; return g; }
        case 'G': { static const uint8_t g[7]={14,17,16,23,17,17,14}; return g; }
        case 'H': { static const uint8_t g[7]={17,17,17,31,17,17,17}; return g; }
        case 'I': { static const uint8_t g[7]={14,4,4,4,4,4,14}; return g; }
        case 'J': { static const uint8_t g[7]={7,2,2,2,18,18,12}; return g; }
        case 'K': { static const uint8_t g[7]={17,18,20,24,20,18,17}; return g; }
        case 'L': { static const uint8_t g[7]={16,16,16,16,16,16,31}; return g; }
        case 'M': { static const uint8_t g[7]={17,27,21,21,17,17,17}; return g; }
        case 'N': { static const uint8_t g[7]={17,25,21,19,17,17,17}; return g; }
        case 'O': { static const uint8_t g[7]={14,17,17,17,17,17,14}; return g; }
        case 'P': { static const uint8_t g[7]={30,17,17,30,16,16,16}; return g; }
        case 'Q': { static const uint8_t g[7]={14,17,17,17,21,18,13}; return g; }
        case 'R': { static const uint8_t g[7]={30,17,17,30,20,18,17}; return g; }
        case 'S': { static const uint8_t g[7]={15,16,16,14,1,1,30}; return g; }
        case 'T': { static const uint8_t g[7]={31,4,4,4,4,4,4}; return g; }
        case 'U': { static const uint8_t g[7]={17,17,17,17,17,17,14}; return g; }
        case 'V': { static const uint8_t g[7]={17,17,17,17,17,10,4}; return g; }
        case 'W': { static const uint8_t g[7]={17,17,17,21,21,21,10}; return g; }
        case 'X': { static const uint8_t g[7]={17,17,10,4,10,17,17}; return g; }
        case 'Y': { static const uint8_t g[7]={17,17,10,4,4,4,4}; return g; }
        case 'Z': { static const uint8_t g[7]={31,1,2,4,8,16,31}; return g; }
        default: return box;
    }
}

static int ndk_string_copy(const void *s, char *out, int cap) {
    if (!s || !out || cap <= 1) return 0;
    const uint8_t *p = (const uint8_t *)s;
    uint32_t size = 0;
    const char *data = NULL;

    if (p[0] & 1) {
        size = *(const uint32_t *)(p + 4);
        data = *(const char * const *)(p + 8);
    } else {
        size = p[0] >> 1;
        data = (const char *)(p + 1);
    }
    if (!data || size >= (uint32_t)cap) size = (uint32_t)cap - 1;
    if (size > 160) size = 160;
    for (uint32_t i = 0; i < size; ++i) {
        char c = data[i];
        if (c == '\n' || c == '\r') c = '\n';
        else if ((unsigned char)c < 32 || (unsigned char)c > 126) c = ' ';
        out[i] = c;
    }
    out[size] = 0;
    return (int)size;
}

typedef struct TextTransform {
    GLfloat mv[16];
    GLfloat pr[16];
    GLint vp[4];
} TextTransform;

static void mul_mat_vec(const GLfloat *m, const GLfloat *v, GLfloat *out) {
    out[0] = m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12] * v[3];
    out[1] = m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13] * v[3];
    out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
    out[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];
}

static void transform_point(const TextTransform *tx, float x, float y, float *sx, float *sy) {
    GLfloat in[4] = { x, y, 0.0f, 1.0f };
    GLfloat eye[4], clip[4];
    mul_mat_vec(tx->mv, in, eye);
    mul_mat_vec(tx->pr, eye, clip);
    float inv_w = clip[3] != 0.0f ? (1.0f / clip[3]) : 1.0f;
    float ndc_x = clip[0] * inv_w;
    float ndc_y = clip[1] * inv_w;
    *sx = tx->vp[0] + (ndc_x * 0.5f + 0.5f) * tx->vp[2];
    *sy = tx->vp[1] + (ndc_y * 0.5f + 0.5f) * tx->vp[3];
}

static void fill_rect_screen(int ix, int iy_top, int iw, int ih) {
    if (ix < 0) { iw += ix; ix = 0; }
    if (iy_top < 0) { ih += iy_top; iy_top = 0; }
    if (ix + iw > 960) iw = 960 - ix;
    if (iy_top + ih > 544) ih = 544 - iy_top;
    if (iw <= 0 || ih <= 0) return;
    glScissor(ix, 544 - iy_top - ih, iw, ih);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void fill_rect(const TextTransform *tx, float x, float y_top, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) return;
    float xs[4], ys[4];
    transform_point(tx, x,     y_top,     &xs[0], &ys[0]);
    transform_point(tx, x + w, y_top,     &xs[1], &ys[1]);
    transform_point(tx, x,     y_top + h, &xs[2], &ys[2]);
    transform_point(tx, x + w, y_top + h, &xs[3], &ys[3]);

    float minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }

    int ix = (int)(minx - 0.5f);
    int iy = (int)(miny - 0.5f);
    int iw = (int)(maxx - minx + 1.5f);
    int ih = (int)(maxy - miny + 1.5f);
    if (ix < 0) { iw += ix; ix = 0; }
    if (iy < 0) { ih += iy; iy = 0; }
    if (ix + iw > 960) iw = 960 - ix;
    if (iy + ih > 544) ih = 544 - iy;
    if (iw <= 0 || ih <= 0) return;
    glScissor(ix, iy, iw, ih);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void draw_ascii_text(int x, int y, const char *text, int r, int g, int b, int align_right, int scale) {
    if (!text || !*text) return;
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;

    int len = (int)strlen(text);
    int char_w = 6 * scale;
    int text_w = len * char_w;
    if (align_right) x -= text_w;
    y -= 7 * scale; // OF coordinates are normally baseline-ish.

    TextTransform tx;
    GLfloat old_clear[4] = {0, 0, 0, 1};
    GLboolean had_scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetFloatv(GL_MODELVIEW_MATRIX, tx.mv);
    glGetFloatv(GL_PROJECTION_MATRIX, tx.pr);
    glGetIntegerv(GL_VIEWPORT, tx.vp);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);

    // Draw transformed scissor clears. This is less pretty than real font
    // shaders, but it is safe with vitaGL and now follows OF's active matrices,
    // unlike the first absolute-overlay fallback.
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    for (int i = 0; i < len; ++i) {
        const uint8_t *rows = glyph5x7(text[i]);
        int gx = x + i * char_w;
        for (int yy = 0; yy < 7; ++yy) {
            for (int xx = 0; xx < 5; ++xx) {
                if (rows[yy] & (1 << (4 - xx))) {
                    fill_rect(&tx, (float)(gx + xx * scale), (float)(y + yy * scale), (float)scale, (float)scale);
                    // Safety net while shader text is still being replaced:
                    // also draw in direct screen coordinates so text remains
                    // visible if vitaGL doesn't expose the current OF matrices.
                    fill_rect_screen(gx + xx * scale, y + yy * scale, scale, scale);
                }
            }
        }
    }
    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    if (!had_scissor) glDisable(GL_SCISSOR_TEST);
}

typedef struct NativeTextEntry {
    int x, y, r, g, b, align, scale;
    uint32_t frame_seen;
    char text[192];
} NativeTextEntry;

static NativeTextEntry g_text_entries[96];
static int g_text_entry_count = 0;
static uint32_t g_text_frame = 0;
static int g_text_screen = 0;
static int g_last_text_screen = 0;

typedef struct NativeFontAtlas {
    int ready;
    int tex_ready;
    int pixel_height;
    GLuint tex;
    stbtt_bakedchar chars[FONT_CHAR_COUNT];
    unsigned char bitmap[FONT_ATLAS_W * FONT_ATLAS_H];
} NativeFontAtlas;

static unsigned char *g_font_data = NULL;
static int g_font_data_size = 0;
static stbtt_fontinfo g_font_info;
static int g_font_info_ready = 0;
static NativeFontAtlas g_font_atlas[11];
static so_hook g_drawleftbutton_hook;
static so_hook g_drawrightbutton_hook;

static int native_font_load(void) {
    if (g_font_info_ready) return 1;
    SceUID fd = sceIoOpen(FONT_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (sz <= 0 || sz > 256 * 1024) {
        sceIoClose(fd);
        return 0;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)sz);
    if (!data) {
        sceIoClose(fd);
        return 0;
    }
    int got = sceIoRead(fd, data, (SceSize)sz);
    sceIoClose(fd);
    if (got != (int)sz || !stbtt_InitFont(&g_font_info, data, 0)) {
        free(data);
        return 0;
    }
    g_font_data = data;
    g_font_data_size = (int)sz;
    g_font_info_ready = 1;
    text_diag_append("native_font loaded bumpitup.ttf\n");
    return 1;
}

static int native_font_pixel_height(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 10) scale = 10;
    return 8 * scale;
}

static NativeFontAtlas *native_font_bake_for_scale(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 10) scale = 10;
    if (!native_font_load()) return NULL;
    NativeFontAtlas *atlas = &g_font_atlas[scale];
    if (!atlas->ready) {
        memset(atlas->bitmap, 0, sizeof(atlas->bitmap));
        atlas->pixel_height = native_font_pixel_height(scale);
        int baked = stbtt_BakeFontBitmap(g_font_data, 0, (float)atlas->pixel_height, atlas->bitmap,
                                         FONT_ATLAS_W, FONT_ATLAS_H, FONT_FIRST_CHAR,
                                         FONT_CHAR_COUNT, atlas->chars);
        if (baked <= 0) return NULL;
        atlas->ready = 1;
    }
    return atlas;
}

static NativeFontAtlas *native_font_atlas_for_scale(int scale) {
    NativeFontAtlas *atlas = native_font_bake_for_scale(scale);
    if (!atlas) return NULL;
    if (!atlas->tex_ready) {
        glGenTextures(1, &atlas->tex);
        glBindTexture(GL_TEXTURE_2D, atlas->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_ATLAS_W, FONT_ATLAS_H, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, atlas->bitmap);
        atlas->tex_ready = 1;
    }
    return atlas;
}

void native_text_begin_frame(void) {
    g_text_frame++;
    g_text_entry_count = 0;
    g_last_text_screen = g_text_screen;
    g_text_screen = 0;
}

int native_text_get_screen(void) {
    return g_text_screen;
}

static int is_fps_string(const char *s) {
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        digits++;
        s++;
    }
    return digits > 0 && digits <= 3 && !strcmp(s, " FPS");
}

static int text_screen_for_string(const char *s) {
    if (!strcmp(s, "SUPER ") || !strcmp(s, "   HEXAGON") || !strcmp(s, "TAP TO START") || !strcmp(s, "PRESS X TO START") || !strcmp(s, "ACHIEVEMENTS")) return 1;
    if (!strcmp(s, "HEXAGON") || !strcmp(s, "DIFFICULTY:") || !strcmp(s, "BEST TIME:") || !strcmp(s, "MAIN MENU")) return 2;
    if (!strcmp(s, "TOUCH THE LEFT AND RIGHT") || !strcmp(s, "SIDES OF THE SCREEN TO MOVE") ||
        !strcmp(s, "  USE DPAD OR L/R  ") || !strcmp(s, "   TO MOVE   ")) return 3;
    if (!strcmp(s, "GAME OVER") || !strcmp(s, "RETRY") || !strcmp(s, "AGAIN") || !strcmp(s, "PRESS X TO RETRY")) return 4;
    if (!strcmp(s, "CLEAR RECORDS")) return 5;
    if (!strcmp(s, "THIS WILL DELETE YOUR PROGRESS") || !strcmp(s, "TAP TO CONFIRM") ||
        !strcmp(s, "PRESS X TO CONFIRM")) return 6;
    return 0;
}

static void normalize_display_text(char *buf) {
    if (!strcmp(buf, "TAP TO START")) {
        strcpy(buf, "PRESS X TO START");
    } else if (!strcmp(buf, "TAP THE SCREEN TO RETRY")) {
        strcpy(buf, "PRESS X TO RETRY");
    } else if (!strcmp(buf, "TAP TO CONFIRM")) {
        strcpy(buf, "PRESS X TO CONFIRM");
    } else if (!strcmp(buf, "TOUCH THE LEFT AND RIGHT")) {
        strcpy(buf, "  USE DPAD OR L/R  ");
    } else if (!strcmp(buf, "SIDES OF THE SCREEN TO MOVE")) {
        strcpy(buf, "   TO MOVE   ");
    }
}

static int fallback_text_width(const char *text, int scale) {
    if (!text) return 0;
    int max_len = 0;
    int line_len = 0;
    for (const char *p = text; ; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (line_len > max_len) max_len = line_len;
            line_len = 0;
            if (*p == '\0') break;
        } else {
            line_len++;
        }
    }
    return max_len * 6 * scale;
}

static int font_line_width(const char *text, int len, int scale) {
    if (!text || len <= 0) return 0;
    NativeFontAtlas *atlas = native_font_bake_for_scale(scale);
    if (!atlas) return len * 6 * scale;
    float x = 0.0f;
    for (int i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < FONT_FIRST_CHAR || c >= FONT_FIRST_CHAR + FONT_CHAR_COUNT) c = '?';
        x += atlas->chars[c - FONT_FIRST_CHAR].xadvance;
    }
    return (int)(x + 0.5f);
}

static int font_text_width(const char *text, int scale) {
    if (!text) return 0;
    int max_w = 0;
    int line_start = 0;
    for (int i = 0; ; ++i) {
        if (text[i] == '\n' || text[i] == '\0') {
            int w = font_line_width(text + line_start, i - line_start, scale);
            if (w > max_w) max_w = w;
            if (text[i] == '\0') break;
            line_start = i + 1;
        }
    }
    return max_w;
}

static int fallback_text_lines(const char *text) {
    if (!text || !*text) return 0;
    int lines = 1;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') lines++;
    }
    return lines;
}

static int fallback_text_height_for_text(const char *text, int scale) {
    int lines = fallback_text_lines(text);
    if (lines <= 0) lines = 1;
    return (lines * 7 + (lines - 1) * 2) * scale;
}

static int font_text_height_for_text(const char *text, int scale) {
    int lines = fallback_text_lines(text);
    if (lines <= 0) lines = 1;
    int line_h = native_font_pixel_height(scale);
    return lines * line_h + (lines - 1) * (2 * scale);
}

static int font_scale_for_metrics(int font) {
    switch (font) {
        case 1: return 4; // graphicsclass::bigprint uses font slot 1.
        case 2: return 5;
        case 3: return 5;
        case 4: return 6;
        case 5: return 6;
        default: return 3; // graphicsclass::print uses font slot 0.
    }
}

static void append_quad(GLfloat *verts, int *count, float x, float y, float w, float h) {
    if (*count + 12 >= 60000) return;
    GLfloat q[12] = { x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h };
    memcpy(&verts[*count], q, sizeof(q));
    *count += 12;
}

static void append_textured_quad(GLfloat *verts, GLfloat *uvs, int *count, const stbtt_aligned_quad *q) {
    if (*count + 12 >= 60000) return;
    GLfloat v[12] = {
        q->x0, q->y0, q->x1, q->y0, q->x0, q->y1,
        q->x1, q->y0, q->x1, q->y1, q->x0, q->y1
    };
    GLfloat t[12] = {
        q->s0, q->t0, q->s1, q->t0, q->s0, q->t1,
        q->s1, q->t0, q->s1, q->t1, q->s0, q->t1
    };
    memcpy(&verts[*count], v, sizeof(v));
    memcpy(&uvs[*count], t, sizeof(t));
    *count += 12;
}

static void draw_overlay_entry(const NativeTextEntry *e) {
    static GLfloat verts[60000];
    static GLfloat uvs[60000];
    int r = e->r, g = e->g, b = e->b;
    if (r == 0 && g == 0 && b == 0) r = g = b = 255;

    int scale = e->scale;
    if (scale < 1) scale = 1;
    if (scale > 10) scale = 10;
    NativeFontAtlas *atlas = native_font_atlas_for_scale(scale);
    int line_start = 0;
    int x = e->x;
    int y = e->y;
    int line_y = y;

    int vcount = 0;
    if (atlas) {
        for (int i = 0; ; ++i) {
            if (e->text[i] == '\n' || e->text[i] == '\0') {
                int line_len = i - line_start;
                int line_w = font_line_width(e->text + line_start, line_len, scale);
                float fx = (float)x;
                float fy = (float)(line_y + atlas->pixel_height);
                if (e->align == 1) fx -= (float)line_w;
                else if (e->align == 2) fx -= (float)line_w * 0.5f;
                for (int j = 0; j < line_len; ++j) {
                    unsigned char c = (unsigned char)e->text[line_start + j];
                    if (c < FONT_FIRST_CHAR || c >= FONT_FIRST_CHAR + FONT_CHAR_COUNT) c = '?';
                    stbtt_aligned_quad q;
                    stbtt_GetBakedQuad(atlas->chars, FONT_ATLAS_W, FONT_ATLAS_H,
                                       c - FONT_FIRST_CHAR, &fx, &fy, &q, 1);
                    append_textured_quad(verts, uvs, &vcount, &q);
                }
                if (e->text[i] == '\0') break;
                line_start = i + 1;
                line_y += atlas->pixel_height + 2 * scale;
            }
        }
    } else {
        for (int i = 0; ; ++i) {
            if (e->text[i] == '\n' || e->text[i] == '\0') {
                int line_len = i - line_start;
                int line_x = x;
                if (e->align == 1) line_x -= line_len * 6 * scale;
                else if (e->align == 2) line_x -= (line_len * 6 * scale) / 2;
                for (int j = 0; j < line_len; ++j) {
                    const uint8_t *rows = glyph5x7(e->text[line_start + j]);
                    int gx = line_x + j * 6 * scale;
                    for (int yy = 0; yy < 7; ++yy) {
                        int xx = 0;
                        while (xx < 5) {
                            while (xx < 5 && !(rows[yy] & (1 << (4 - xx)))) xx++;
                            int start = xx;
                            while (xx < 5 && (rows[yy] & (1 << (4 - xx)))) xx++;
                            if (xx > start) append_quad(verts, &vcount, (float)(gx + start * scale), (float)(line_y + yy * scale), (float)((xx - start) * scale), (float)scale);
                        }
                    }
                }
                if (e->text[i] == '\0') break;
                line_start = i + 1;
                line_y += 9 * scale;
            }
        }
    }
    if (vcount <= 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 960, 544);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0.0f, 960.0f, 544.0f, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4ub((GLubyte)r, (GLubyte)g, (GLubyte)b, 255);
    if (atlas) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, atlas->tex);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glDrawArrays(GL_TRIANGLES, 0, vcount / 2);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (atlas) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void native_text_render_overlay(void) {
    GLfloat old_clear[4] = {0, 0, 0, 1};
    GLfloat old_modelview[16];
    GLfloat old_projection[16];
    GLint old_viewport[4];
    GLint old_matrix_mode = GL_MODELVIEW;
    GLint old_program = 0;
    GLboolean had_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean had_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean had_texture = glIsEnabled(GL_TEXTURE_2D);
    GLboolean had_blend = glIsEnabled(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetIntegerv(GL_VIEWPORT, old_viewport);
    glGetIntegerv(GL_MATRIX_MODE, &old_matrix_mode);
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glMatrixMode(GL_MODELVIEW);
    glGetFloatv(GL_MODELVIEW_MATRIX, old_modelview);
    glMatrixMode(GL_PROJECTION);
    glGetFloatv(GL_PROJECTION_MATRIX, old_projection);
    glViewport(0, 0, 960, 544);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
    for (int i = 0; i < g_text_entry_count; ++i) {
        if ((uint32_t)(g_text_frame - g_text_entries[i].frame_seen) <= 600) {
            draw_overlay_entry(&g_text_entries[i]);
        }
    }
    glUseProgram((GLuint)old_program);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(old_projection);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(old_modelview);
    glMatrixMode(old_matrix_mode);
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    if (had_depth) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    if (had_texture) glEnable(GL_TEXTURE_2D);
    else glDisable(GL_TEXTURE_2D);
    if (had_blend) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    if (!had_scissor) glDisable(GL_SCISSOR_TEST);
    else glEnable(GL_SCISSOR_TEST);
}

static void transform_text_anchor(int x, int y, int *out_x, int *out_y, int *out_scale, int scale) {
    TextTransform tx;
    glGetFloatv(GL_MODELVIEW_MATRIX, tx.mv);
    glGetFloatv(GL_PROJECTION_MATRIX, tx.pr);
    glGetIntegerv(GL_VIEWPORT, tx.vp);

    float sx0, sy0, sx1, sy1, sx2, sy2;
    transform_point(&tx, (float)x, (float)y, &sx0, &sy0);
    transform_point(&tx, (float)(x + 1), (float)y, &sx1, &sy1);
    transform_point(&tx, (float)x, (float)(y + 1), &sx2, &sy2);

    float top_y = 544.0f - sy0;
    float unit_x = fabsf(sx1 - sx0);
    float unit_y = fabsf((544.0f - sy2) - top_y);
    float unit = unit_x > unit_y ? unit_x : unit_y;

    if (sx0 > -1920.0f && sx0 < 2880.0f && top_y > -1088.0f && top_y < 1632.0f && unit > 0.05f && unit < 16.0f) {
        *out_x = (int)(sx0 + (sx0 >= 0.0f ? 0.5f : -0.5f));
        *out_y = (int)(top_y + (top_y >= 0.0f ? 0.5f : -0.5f));
        int scaled = (int)(scale * unit + 0.5f);
        if (scaled < 1) scaled = 1;
        if (scaled > 10) scaled = 10;
        *out_scale = scaled;
        return;
    }

    *out_x = x;
    *out_y = y;
    *out_scale = scale;
}

static void print_common(int x, int y, const void *s, int r, int g, int b, int align, int scale) {
    static int diag_count = 0;
    static char diag_seen[128][96];
    char buf[192];
    int n = ndk_string_copy(s, buf, sizeof(buf));
    if (n <= 0) return;
    normalize_display_text(buf);
    int fps_string = is_fps_string(buf);
    int sx = x;
    int sy = y;
    int draw_scale = scale;
    transform_text_anchor(x, y, &sx, &sy, &draw_scale, scale);
    if (align == 2 && sx > -240 && sx < 240) sx += 480;
    if (!strcmp(buf, "SUPER ") || !strcmp(buf, "   HEXAGON")) {
        draw_scale = 9;
    }
    int seen = 0;
    for (int i = 0; i < diag_count; ++i) {
        if (!strcmp(diag_seen[i], fps_string ? "<FPS>" : buf)) { seen = 1; break; }
    }
    if (!seen && diag_count < 128) {
        strncpy(diag_seen[diag_count], fps_string ? "<FPS>" : buf, sizeof(diag_seen[diag_count]) - 1);
        diag_seen[diag_count][sizeof(diag_seen[diag_count]) - 1] = '\0';
        char line[320];
        snprintf(line, sizeof(line), "text_unique[%d] x=%d y=%d sx=%d sy=%d align=%d scale=%d rgb=%d,%d,%d '%s'\n", diag_count, x, y, sx, sy, align, draw_scale, r, g, b, buf);
        text_diag_append(line);
        diag_count++;
    }
    int screen = text_screen_for_string(buf);
    if (screen) g_text_screen = screen;
    if (g_text_screen == 3 && (screen == 1 || screen == 2)) return;
    NativeTextEntry *e = NULL;
    for (int i = 0; i < g_text_entry_count; ++i) {
        if (g_text_entries[i].align == align &&
            g_text_entries[i].scale == draw_scale &&
            g_text_entries[i].x == sx &&
            g_text_entries[i].y == sy &&
            (strcmp(g_text_entries[i].text, buf) == 0 || (fps_string && is_fps_string(g_text_entries[i].text)))) {
            e = &g_text_entries[i];
            break;
        }
    }
    if (!e) {
        for (int i = 0; i < g_text_entry_count; ++i) {
            if (g_text_entries[i].align == align &&
                g_text_entries[i].scale == draw_scale &&
                g_text_entries[i].x == sx &&
                g_text_entries[i].y == sy) {
                e = &g_text_entries[i];
                break;
            }
        }
    }
    if (!e) {
        if (g_text_entry_count >= (int)(sizeof(g_text_entries) / sizeof(g_text_entries[0]))) {
            int oldest = 0;
            for (int i = 1; i < g_text_entry_count; ++i) {
                if ((uint32_t)(g_text_entries[oldest].frame_seen - g_text_entries[i].frame_seen) < 0x80000000u) oldest = i;
            }
            e = &g_text_entries[oldest];
        } else {
            e = &g_text_entries[g_text_entry_count++];
        }
    }
    e->frame_seen = g_text_frame;
    e->x = sx;
    e->y = sy;
    e->r = r;
    e->g = g;
    e->b = b;
    e->align = align;
    e->scale = draw_scale;
    strncpy(e->text, buf, sizeof(e->text) - 1);
    e->text[sizeof(e->text) - 1] = '\0';
}

void native_graphics_print(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered) {
    (void)self;
    print_common(x, y, ndk_string, r, g, b, centered ? 2 : 0, 3);
}

void native_graphics_rprint(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered) {
    (void)self; (void)centered;
    print_common(x, y, ndk_string, r, g, b, 1, 3);
}

void native_graphics_bigprint(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered, int size) {
    (void)self;
    int scale = size > 1 ? 5 : 4;
    print_common(x, y, ndk_string, r, g, b, centered ? 2 : 0, scale);
}

void native_graphics_rbigprint(void *self, int x, int y, const void *ndk_string, int r, int g, int b, int centered, int size) {
    (void)self; (void)centered;
    int scale = size > 1 ? 5 : 4;
    print_common(x, y, ndk_string, r, g, b, 1, scale);
}

static void continue_drawbutton_hook(so_hook *h, void *self, void *graphics, int x, int y, int active) {
    kuKernelCpuUnrestrictedMemcpy((void *)h->addr, h->orig_instr, sizeof(h->orig_instr));
    kuKernelFlushCaches((void *)h->addr, sizeof(h->orig_instr));
    if (h->thumb_addr) ((void (*)(void *, void *, int, int, int))h->thumb_addr)(self, graphics, x, y, active);
    else ((void (*)(void *, void *, int, int, int))h->addr)(self, graphics, x, y, active);
    kuKernelCpuUnrestrictedMemcpy((void *)h->addr, h->patch_instr, sizeof(h->patch_instr));
    kuKernelFlushCaches((void *)h->addr, sizeof(h->patch_instr));
}

static void native_gameclass_drawleftbutton(void *self, void *graphics, int x, int y, int active) {
    if (g_last_text_screen == 0 || g_last_text_screen == 3) return;
    continue_drawbutton_hook(&g_drawleftbutton_hook, self, graphics, x, y, active);
}

static void native_gameclass_drawrightbutton(void *self, void *graphics, int x, int y, int active) {
    if (g_last_text_screen == 0 || g_last_text_screen == 3) return;
    continue_drawbutton_hook(&g_drawrightbutton_hook, self, graphics, x, y, active);
}

int native_graphics_len(void *self, const void *ndk_string, int font) {
    (void)self;
    char buf[192];
    if (ndk_string_copy(ndk_string, buf, sizeof(buf)) <= 0) return 0;
    normalize_display_text(buf);
    int scale = font_scale_for_metrics(font);
    int w = font_text_width(buf, scale);
    return w > 0 ? w : fallback_text_width(buf, scale);
}

int native_graphics_hig(void *self, const void *ndk_string, int font) {
    (void)self;
    char buf[192];
    if (ndk_string_copy(ndk_string, buf, sizeof(buf)) <= 0) return 0;
    normalize_display_text(buf);
    if (!strcmp(buf, "LEVEL UP")) {
        // The in-game level-up HUD draws "LEVEL UP" and the unlocked shape as
        // separate text calls in the same background area.
        return (27 + native_font_pixel_height(5)) - 4;
    }
    return font_text_height_for_text(buf, font_scale_for_metrics(font));
}

int native_graphics_hig_text(void *self, int text, int font) {
    (void)self;
    if (font == 2 && text >= 8 && text <= 12) {
        // The level-up HUD measures only the unlocked-shape Text enum here,
        // but the drawn block also includes the small "LEVEL UP" label above it.
        return (27 + native_font_pixel_height(5)) - 4;
    }
    return font_text_height_for_text("X", font_scale_for_metrics(font));
}

void native_text_install_hooks(void *superhex_mod_ptr) {
    so_module *mod = (so_module *)superhex_mod_ptr;
    const char *print = "_ZNK13graphicsclass5printEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiib";
    const char *rprint = "_ZNK13graphicsclass6rprintEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiib";
    const char *printsafe = "_ZNK13graphicsclass9printsafeEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiib";
    const char *pctrl = "_ZNK13graphicsclass15printcontrollerEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiib";
    const char *rpctrl = "_ZNK13graphicsclass16rprintcontrollerEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiib";
    const char *big = "_ZNK13graphicsclass8bigprintEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiibi";
    const char *rbig = "_ZNK13graphicsclass9rbigprintEiiRKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEiiibi";
    const char *len = "_ZNK13graphicsclass3lenERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEi";
    const char *lenutf = "_ZNK13graphicsclass6lenUTFERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEi";
    const char *lenctrl = "_ZNK13graphicsclass13lenControllerERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEi";
    const char *lentex = "_ZNK13graphicsclass11len_textureERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEi";
    const char *hig = "_ZNK13graphicsclass3higERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEi";
    const char *higctrl = "_ZNK13graphicsclass13higControllerERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEi";
    const char *higtext = "_ZNK13graphicsclass3higE4Texti";
    const char *drawleftbutton = "_ZN9gameclass14drawleftbuttonER13graphicsclassiib";
    const char *drawrightbutton = "_ZN9gameclass15drawrightbuttonER13graphicsclassiib";

    uintptr_t a;
    if ((a = so_symbol(mod, print))) hook_addr(a, (uintptr_t)&native_graphics_print);
    if ((a = so_symbol(mod, rprint))) hook_addr(a, (uintptr_t)&native_graphics_rprint);
    if ((a = so_symbol(mod, printsafe))) hook_addr(a, (uintptr_t)&native_graphics_print);
    if ((a = so_symbol(mod, pctrl))) hook_addr(a, (uintptr_t)&native_graphics_print);
    if ((a = so_symbol(mod, rpctrl))) hook_addr(a, (uintptr_t)&native_graphics_rprint);
    if ((a = so_symbol(mod, big))) hook_addr(a, (uintptr_t)&native_graphics_bigprint);
    if ((a = so_symbol(mod, rbig))) hook_addr(a, (uintptr_t)&native_graphics_rbigprint);
    if ((a = so_symbol(mod, len))) hook_addr(a, (uintptr_t)&native_graphics_len);
    if ((a = so_symbol(mod, lenutf))) hook_addr(a, (uintptr_t)&native_graphics_len);
    if ((a = so_symbol(mod, lenctrl))) hook_addr(a, (uintptr_t)&native_graphics_len);
    if ((a = so_symbol(mod, lentex))) hook_addr(a, (uintptr_t)&native_graphics_len);
    if ((a = so_symbol(mod, hig))) hook_addr(a, (uintptr_t)&native_graphics_hig);
    if ((a = so_symbol(mod, higctrl))) hook_addr(a, (uintptr_t)&native_graphics_hig);
    if ((a = so_symbol(mod, higtext))) hook_addr(a, (uintptr_t)&native_graphics_hig_text);
    if ((a = so_symbol(mod, drawleftbutton))) g_drawleftbutton_hook = hook_addr(a, (uintptr_t)&native_gameclass_drawleftbutton);
    if ((a = so_symbol(mod, drawrightbutton))) g_drawrightbutton_hook = hook_addr(a, (uintptr_t)&native_gameclass_drawrightbutton);
    l_info("Installed native block-text graphics hooks");
}
