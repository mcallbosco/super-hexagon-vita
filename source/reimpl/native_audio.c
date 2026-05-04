#include "reimpl/native_audio.h"

#include "so_util/so_util.h"
#include "utils/logger.h"

#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_FRAMES 1024
#define AUDIO_BUFFERS 4
#define OUTPUT_RATE 48000
#define MAX_SFX 16
#define MAX_VOICES 8
#define SFX_QUEUE_SIZE 32
#define VOICE_FADE_FRAMES 96
#define SFX_RETRIGGER_GUARD_FRAMES 2048
#define MUSIC_VOLUME 128
#define DIAG_PATH DATA_PATH "native_diag.txt"

typedef struct WavInfo {
    int channels;
    int rate;
    int bits;
    uint32_t data_off;
    uint32_t data_size;
} WavInfo;

typedef struct AudioSample {
    int16_t *pcm;
    uint32_t frames;
    int channels;
    int rate;
    int source_rate;
    uint32_t source_frames;
} AudioSample;

typedef struct MusicStream {
    SceUID fd;
    uint32_t data_off;
    uint32_t data_size;
    uint32_t frames;
    uint32_t pos_frame;
    int channels;
    int rate;
    int song;
} MusicStream;

typedef struct SfxVoice {
    volatile int active;
    int sample;
    uint64_t phase; // 16.16 source frame index
    uint64_t step;
    int volume; // 0..256
} SfxVoice;

static volatile int g_audio_running = 0;
static SceUID g_audio_thread = -1;
static MusicStream g_music = { .fd = -1, .song = -1 };
static AudioSample g_sfx[MAX_SFX];
static SfxVoice g_voices[MAX_VOICES];
static volatile uint32_t g_sfx_queue_head = 0;
static volatile uint32_t g_sfx_queue_tail = 0;
static int g_sfx_queue[SFX_QUEUE_SIZE];
static uint32_t g_clip_events = 0;
static uint32_t g_mix_blocks = 0;
static volatile int g_music_clock_running = 0;
static volatile int g_music_playing = 0;
static volatile int g_music_song = -1;
static volatile int g_music_requested_song = -1;
static volatile int g_music_requested_ms = -1;
static volatile int g_music_stop_requested = 0;
static int g_music_loaded_song = -1;
static uint64_t g_music_clock_start_us = 0;
static volatile int g_music_clock_offset_ms = 0;
static volatile int g_music_play_count = 0;
static volatile int g_music_section_resume_song = -1;
static void (*g_orig_loadsongdata)(void *self, int song) = NULL;

#define MUSIC_FIELD_CURRENT_SONG 0x13d60
#define MUSIC_FIELD_PLAY_COUNT 0x13d64
#define MUSIC_FIELD_FADE 0x13d6c
#define MUSIC_FIELD_LAST_ELAPSED_MS 0x13d78
#define MUSIC_FIELD_LENGTH_MS 0x13d80
#define MUSIC_FIELD_POSITION_MS 0x13d84

uint64_t native_port_elapsed_ms(void);

static const int g_music_lengths_ms[] = {
    0, 189000, 145000, 161000, 77000, 162000,
};

static const int g_music1_offsets_ms[] = {
    0, 27477, 80410, 110000,
};

static const int g_music2_offsets_ms[] = {
    0, 29000, 44000, 44000, 57500, 29000, 44000, 44000, 57500,
};

static const int g_music3_offsets_ms[] = {
    0, 30000, 45000,
};

static void sync_game_music_state(void *self, int song, int start_ms, int playing) {
    if (!self) return;
    char *m = (char *)self;
    if (!playing || song < 0) {
        *(int *)(m + MUSIC_FIELD_CURRENT_SONG) = -1;
        *(int *)(m + MUSIC_FIELD_LENGTH_MS) = 0;
        *(int *)(m + MUSIC_FIELD_POSITION_MS) = 0;
        return;
    }

    int length_ms = ((unsigned)song < (sizeof(g_music_lengths_ms) / sizeof(g_music_lengths_ms[0]))) ? g_music_lengths_ms[song] : 0;
    *(int *)(m + MUSIC_FIELD_CURRENT_SONG) = song;
    *(int *)(m + MUSIC_FIELD_LENGTH_MS) = length_ms;
    *(float *)(m + MUSIC_FIELD_FADE) = 30.0f;
    *(uint64_t *)(m + MUSIC_FIELD_LAST_ELAPSED_MS) = native_port_elapsed_ms();
    *(int *)(m + MUSIC_FIELD_POSITION_MS) = start_ms < 0 ? 0 : start_ms;
    *(int *)(m + MUSIC_FIELD_PLAY_COUNT) = *(int *)(m + MUSIC_FIELD_PLAY_COUNT) + 1;

    if (g_orig_loadsongdata) {
        g_orig_loadsongdata(self, song);
    }
}

static const char *g_sfx_names[MAX_SFX] = {
    // Order is the actual musicclass::loadsoundeffects() slot order. playef(id)
    // indexes this array directly in the original game.
    "sounds/begin.wav", "sounds/excellent.wav", "sounds/gameover.wav", "sounds/start.wav",
    "sounds/die.wav", "sounds/rankup.wav", "sounds/line.wav", "sounds/triangle.wav",
    "sounds/square.wav", "sounds/pentagon.wav", "sounds/hexagon.wav", "sounds/menuselect.wav",
    "sounds/menuchoose.wav", "sounds/awesome.wav", "sounds/wonderful.wav", "sounds/superhexagon.wav",
};

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

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int read_exact(SceUID fd, void *buf, int len) {
    uint8_t *p = (uint8_t *)buf;
    int done = 0;
    while (done < len) {
        int r = sceIoRead(fd, p + done, len - done);
        if (r <= 0) return -1;
        done += r;
    }
    return 0;
}

static int parse_wav(SceUID fd, WavInfo *info) {
    uint8_t hdr[12];
    memset(info, 0, sizeof(*info));
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (read_exact(fd, hdr, sizeof(hdr)) < 0) return -1;
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) return -1;

    for (;;) {
        uint8_t ch[8];
        if (read_exact(fd, ch, sizeof(ch)) < 0) return -1;
        uint32_t size = rd32(ch + 4);
        SceOff pos = sceIoLseek(fd, 0, SCE_SEEK_CUR);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[32];
            int want = size < sizeof(fmt) ? (int)size : (int)sizeof(fmt);
            if (read_exact(fd, fmt, want) < 0) return -1;
            if (rd16(fmt) != 1) return -1; // PCM only
            info->channels = rd16(fmt + 2);
            info->rate = (int)rd32(fmt + 4);
            info->bits = rd16(fmt + 14);
        } else if (!memcmp(ch, "data", 4)) {
            info->data_off = (uint32_t)pos;
            info->data_size = size;
            break;
        }
        sceIoLseek(fd, pos + ((size + 1) & ~1u), SCE_SEEK_SET);
    }
    return (info->channels == 1 || info->channels == 2) && info->bits == 16 && info->rate > 0 && info->data_size > 0 ? 0 : -1;
}

static int load_sample(const char *path, AudioSample *out) {
    memset(out, 0, sizeof(*out));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return fd;

    WavInfo wav;
    if (parse_wav(fd, &wav) < 0) { sceIoClose(fd); return -1; }
    int16_t *pcm = malloc(wav.data_size);
    if (!pcm) { sceIoClose(fd); return -2; }
    sceIoLseek(fd, wav.data_off, SCE_SEEK_SET);
    if (read_exact(fd, pcm, (int)wav.data_size) < 0) { free(pcm); sceIoClose(fd); return -3; }
    sceIoClose(fd);

    uint32_t frames = wav.data_size / ((uint32_t)wav.channels * 2u);
    out->source_rate = wav.rate;
    out->source_frames = frames;
    if (wav.rate != OUTPUT_RATE) {
        uint32_t out_frames = (uint32_t)((((uint64_t)frames * OUTPUT_RATE) + (wav.rate / 2)) / wav.rate);
        int16_t *resampled = malloc((size_t)out_frames * wav.channels * sizeof(int16_t));
        if (!resampled) { free(pcm); return -4; }

        for (uint32_t i = 0; i < out_frames; ++i) {
            uint64_t phase = (((uint64_t)i * wav.rate) << 16) / OUTPUT_RATE;
            uint32_t idx = (uint32_t)(phase >> 16);
            uint32_t next = idx + 1;
            uint32_t frac = (uint32_t)(phase & 0xffffu);
            if (idx >= frames) idx = frames - 1;
            if (next >= frames) next = idx;
            for (int c = 0; c < wav.channels; ++c) {
                int s0 = pcm[idx * wav.channels + c];
                int s1 = pcm[next * wav.channels + c];
                resampled[i * wav.channels + c] = (int16_t)(s0 + (int)((((int64_t)s1 - s0) * frac) >> 16));
            }
        }

        free(pcm);
        pcm = resampled;
        frames = out_frames;
    }

    out->pcm = pcm;
    out->frames = frames;
    out->channels = wav.channels;
    out->rate = OUTPUT_RATE;
    return 0;
}

static int open_music_stream(int song, int start_ms) {
    char path[256];
    WavInfo wav;
    if (song < 0 || song > 5) return -1;

    if (g_music.fd >= 0) {
        sceIoClose(g_music.fd);
        g_music.fd = -1;
    }

    snprintf(path, sizeof(path), DATA_PATH "assets/music/music%d.dat", song);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return fd;
    if (parse_wav(fd, &wav) < 0 || wav.rate != OUTPUT_RATE || (wav.channels != 1 && wav.channels != 2)) {
        sceIoClose(fd);
        return -2;
    }

    uint32_t frames = wav.data_size / ((uint32_t)wav.channels * 2u);
    uint32_t start_frame = frames ? (uint32_t)((((uint64_t)(start_ms < 0 ? 0 : start_ms) * OUTPUT_RATE) / 1000u) % frames) : 0;
    uint32_t frame_bytes = (uint32_t)wav.channels * 2u;
    sceIoLseek(fd, wav.data_off + (SceOff)start_frame * frame_bytes, SCE_SEEK_SET);

    g_music.fd = fd;
    g_music.data_off = wav.data_off;
    g_music.data_size = wav.data_size;
    g_music.frames = frames;
    g_music.pos_frame = start_frame;
    g_music.channels = wav.channels;
    g_music.rate = wav.rate;
    g_music.song = song;
    g_music_loaded_song = song;
    return 0;
}

static int clamp16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

static int pick_index(int count) {
    if (count <= 1) return 0;
    return rand() % count;
}

static int music_play_start_ms(int song) {
    if (g_music_section_resume_song != song) return 0;
    if (song == 1) {
        return g_music1_offsets_ms[pick_index((int)(sizeof(g_music1_offsets_ms) / sizeof(g_music1_offsets_ms[0])))];
    }
    if (song == 2) {
        return g_music2_offsets_ms[pick_index((int)(sizeof(g_music2_offsets_ms) / sizeof(g_music2_offsets_ms[0])))];
    }
    if (song == 3) {
        return g_music3_offsets_ms[pick_index((int)(sizeof(g_music3_offsets_ms) / sizeof(g_music3_offsets_ms[0])))];
    }
    return 0;
}

static void sample_at(const AudioSample *s, uint64_t phase, int *l, int *r) {
    if (!s->pcm || !s->frames) { *l = *r = 0; return; }
    uint32_t idx = (uint32_t)(phase >> 16);
    if (idx >= s->frames) { *l = *r = 0; return; }
    uint32_t frac = (uint32_t)(phase & 0xffffu);
    if (frac == 0) {
        if (s->channels == 2) {
            *l = s->pcm[idx * 2 + 0];
            *r = s->pcm[idx * 2 + 1];
        } else {
            *l = *r = s->pcm[idx];
        }
        return;
    }
    uint32_t next = idx + 1;
    if (next >= s->frames) next = idx;
    if (s->channels == 2) {
        int l0 = s->pcm[idx * 2 + 0], r0 = s->pcm[idx * 2 + 1];
        int l1 = s->pcm[next * 2 + 0], r1 = s->pcm[next * 2 + 1];
        *l = l0 + (int)((((int64_t)l1 - l0) * frac) >> 16);
        *r = r0 + (int)((((int64_t)r1 - r0) * frac) >> 16);
    } else {
        int s0 = s->pcm[idx], s1 = s->pcm[next];
        *l = *r = s0 + (int)((((int64_t)s1 - s0) * frac) >> 16);
    }
}

static void load_sfx_all(void) {
    char line[128];
    for (int i = 0; i < MAX_SFX; ++i) {
        char path[256];
        snprintf(path, sizeof(path), DATA_PATH "assets/%s", g_sfx_names[i]);
        int r = load_sample(path, &g_sfx[i]);
        snprintf(line, sizeof(line), "sfx[%d] %s r=%d src=%u@%d out=%u@%d\n",
                 i, g_sfx_names[i], r, g_sfx[i].source_frames, g_sfx[i].source_rate, g_sfx[i].frames, g_sfx[i].rate);
        diag_append(line);
    }
}

static void unload_music(void) {
    if (g_music.fd >= 0) {
        sceIoClose(g_music.fd);
    }
    memset(&g_music, 0, sizeof(g_music));
    g_music.fd = -1;
    g_music.song = -1;
    g_music_loaded_song = -1;
}

static void set_music_position_ms(int ms) {
    if (ms < 0) ms = 0;
    g_music_clock_offset_ms = ms;
    g_music_clock_start_us = (uint64_t)sceKernelGetSystemTimeWide();
    if (g_music.fd < 0 || !g_music.frames) return;
    uint64_t frame = ((uint64_t)ms * OUTPUT_RATE) / 1000u;
    if (g_music.frames) frame %= g_music.frames;
    g_music.pos_frame = (uint32_t)frame;
    sceIoLseek(g_music.fd, g_music.data_off + (SceOff)g_music.pos_frame * (g_music.channels * 2), SCE_SEEK_SET);
}

static void load_music_song(int song) {
    char line[128];
    if (song < 0 || song > 5) {
        snprintf(line, sizeof(line), "music load ignored song=%d\n", song);
        diag_append(line);
        return;
    }
    if (g_music_loaded_song == song && g_music.fd >= 0) return;

    unload_music();
    int start_ms = g_music_requested_ms < 0 ? 0 : g_music_requested_ms;
    int r = open_music_stream(song, start_ms);
    if (r == 0) set_music_position_ms(start_ms);
    snprintf(line, sizeof(line), "music stream song=%d r=%d frames=%u@%d ch=%d start=%d\n",
             song, r, g_music.frames, g_music.rate, g_music.channels, start_ms);
    diag_append(line);
}

static void apply_music_requests(void) {
    if (g_music_stop_requested) {
        g_music_stop_requested = 0;
        g_music_requested_song = -1;
    }
    int song = g_music_requested_song;
    if (song >= 0 && song != g_music_loaded_song) {
        load_music_song(song);
    } else if (song >= 0 && g_music_loaded_song == song && g_music.fd >= 0) {
        set_music_position_ms(g_music_requested_ms < 0 ? 0 : g_music_requested_ms);
        g_music_requested_song = -1;
    }
}

void native_audio_play_sfx(int id) {
    if (id < 0) id = -id;
    if (id >= MAX_SFX) {
        char line[64]; snprintf(line, sizeof(line), "play_sfx ignored id=%d\n", id); diag_append(line);
        return;
    }
    uint32_t head = g_sfx_queue_head;
    uint32_t next = (head + 1) % SFX_QUEUE_SIZE;
    if (next == g_sfx_queue_tail) return;
    g_sfx_queue[head] = id;
    __sync_synchronize();
    g_sfx_queue_head = next;
}

static void start_voice(int sample) {
    if (!g_sfx[sample].pcm || !g_sfx[sample].frames) return;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (g_voices[i].active && g_voices[i].sample == sample &&
            (g_voices[i].phase >> 16) < SFX_RETRIGGER_GUARD_FRAMES) {
            return;
        }
    }
    int slot = -1;
    for (int i = 0; slot < 0 && i < MAX_VOICES; ++i) if (!g_voices[i].active) { slot = i; break; }
    if (slot < 0) slot = sample % MAX_VOICES;
    g_voices[slot].active = 0;
    g_voices[slot].sample = sample;
    g_voices[slot].phase = 0;
    g_voices[slot].step = ((uint64_t)g_sfx[sample].rate << 16) / OUTPUT_RATE;
    if (!g_voices[slot].step) g_voices[slot].step = 1 << 16;
    g_voices[slot].volume = 72;
    __sync_synchronize();
    g_voices[slot].active = 1;
}

static void drain_sfx_queue(void) {
    while (g_sfx_queue_tail != g_sfx_queue_head) {
        uint32_t tail = g_sfx_queue_tail;
        int sample = g_sfx_queue[tail];
        g_sfx_queue_tail = (tail + 1) % SFX_QUEUE_SIZE;
        if ((unsigned)sample < MAX_SFX) start_voice(sample);
    }
}

static void mix_music_stream(int32_t *mix, int frames) {
    if (!g_music_playing || g_music.fd < 0 || !g_music.frames) return;

    int16_t pcm[AUDIO_FRAMES * 2];
    int frame_bytes = g_music.channels * 2;
    int mixed = 0;
    while (mixed < frames) {
        uint32_t remaining_track = g_music.frames - g_music.pos_frame;
        int want = frames - mixed;
        if (remaining_track < (uint32_t)want) want = (int)remaining_track;
        if (want <= 0) {
            g_music.pos_frame = 0;
            sceIoLseek(g_music.fd, g_music.data_off, SCE_SEEK_SET);
            if (g_music_clock_running) {
                g_music_clock_start_us = (uint64_t)sceKernelGetSystemTimeWide();
                g_music_clock_offset_ms = 0;
            }
            continue;
        }

        int bytes = want * frame_bytes;
        int got = sceIoRead(g_music.fd, pcm, bytes);
        if (got <= 0) {
            g_music.pos_frame = 0;
            sceIoLseek(g_music.fd, g_music.data_off, SCE_SEEK_SET);
            continue;
        }
        int got_frames = got / frame_bytes;
        for (int i = 0; i < got_frames; ++i) {
            int l, r;
            if (g_music.channels == 2) {
                l = pcm[i * 2 + 0];
                r = pcm[i * 2 + 1];
            } else {
                l = r = pcm[i];
            }
            int oi = (mixed + i) * 2;
            mix[oi + 0] += (l * MUSIC_VOLUME) >> 8;
            mix[oi + 1] += (r * MUSIC_VOLUME) >> 8;
        }
        mixed += got_frames;
        g_music.pos_frame += (uint32_t)got_frames;
        if (g_music.pos_frame >= g_music.frames) {
            g_music.pos_frame = 0;
            sceIoLseek(g_music.fd, g_music.data_off, SCE_SEEK_SET);
            if (g_music_clock_running) {
                g_music_clock_start_us = (uint64_t)sceKernelGetSystemTimeWide();
                g_music_clock_offset_ms = 0;
            }
        }
        if (got_frames < want) break;
    }
}

static void mix_sfx(int16_t *out, int32_t *mix, int frames) {
    memset(mix, 0, (size_t)frames * 2 * sizeof(int32_t));

    apply_music_requests();
    mix_music_stream(mix, frames);

    drain_sfx_queue();
    for (int v = 0; v < MAX_VOICES; ++v) {
        if (!g_voices[v].active) continue;
        SfxVoice *voice = &g_voices[v];
        AudioSample *s = &g_sfx[voice->sample];
        for (int i = 0; i < frames; ++i) {
            uint32_t idx = voice->phase >> 16;
            if (idx >= s->frames) { voice->active = 0; break; }
            int l, r; sample_at(s, voice->phase, &l, &r);
            uint32_t tail = s->frames - idx;
            uint32_t fade = idx < VOICE_FADE_FRAMES ? idx : VOICE_FADE_FRAMES;
            if (tail < fade) fade = tail;
            int gain = voice->volume;
            if (fade < VOICE_FADE_FRAMES) gain = (gain * (int)fade) / VOICE_FADE_FRAMES;
            int oi = i * 2;
            mix[oi + 0] += (l * gain) >> 8;
            mix[oi + 1] += (r * gain) >> 8;
            voice->phase += voice->step;
        }
    }

    int clipped = 0;
    for (int i = 0; i < frames * 2; ++i) {
        if (mix[i] > 32767 || mix[i] < -32768) clipped = 1;
        out[i] = (int16_t)clamp16(mix[i]);
    }
    if (clipped) g_clip_events++;
    g_mix_blocks++;
}

static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    diag_append("audio_thread start\n");
    load_sfx_all();

    char line[128];
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_FRAMES, OUTPUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    snprintf(line, sizeof(line), "audio port=%d rate=%d\n", port, OUTPUT_RATE);
    diag_append(line);
    if (port < 0) return 0;
    int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);

    int16_t *out[AUDIO_BUFFERS] = {0};
    int32_t *mix = memalign(64, (size_t)AUDIO_FRAMES * 2 * sizeof(int32_t));
    for (int i = 0; i < AUDIO_BUFFERS; ++i) {
        out[i] = memalign(64, (size_t)AUDIO_FRAMES * 2 * sizeof(int16_t));
    }
    int alloc_ok = mix != NULL;
    for (int i = 0; i < AUDIO_BUFFERS; ++i) {
        if (!out[i]) alloc_ok = 0;
    }
    if (!alloc_ok) {
        diag_append("audio alloc failed\n");
        for (int i = 0; i < AUDIO_BUFFERS; ++i) {
            if (out[i]) free(out[i]);
        }
        if (mix) free(mix);
        sceAudioOutReleasePort(port);
        return 0;
    }
    g_audio_running = 1;
    l_info("native audio: streaming SFX through Vita output");

    uint32_t blocks = 0;
    while (g_audio_running) {
        int16_t *buf = out[blocks % AUDIO_BUFFERS];
        memset(buf, 0, (size_t)AUDIO_FRAMES * 2 * sizeof(int16_t));
        mix_sfx(buf, mix, AUDIO_FRAMES);
        int or = sceAudioOutOutput(port, buf);
        if (blocks < 4) { snprintf(line, sizeof(line), "audio output[%u]=%d\n", blocks, or); diag_append(line); }
        if (blocks && (blocks % 512) == 0) {
            g_clip_events = 0;
            g_mix_blocks = 0;
        }
        blocks++;
    }

    free(mix);
    for (int i = 0; i < AUDIO_BUFFERS; ++i) free(out[i]);
    sceAudioOutReleasePort(port);
    return 0;
}

void native_audio_start(void) {
#ifdef DEBUG_SOLOADER
    sceIoRemove(DIAG_PATH);
#endif
    char start_line[80];
    snprintf(start_line, sizeof(start_line), "native_audio_start rate=%d frames=%d buffers=%d volume=72 fade=%d\n",
             OUTPUT_RATE, AUDIO_FRAMES, AUDIO_BUFFERS, VOICE_FADE_FRAMES);
    diag_append(start_line);
    if (g_audio_thread >= 0) return;
    g_audio_thread = sceKernelCreateThread("shx_native_audio", audio_thread, 0x40, 0x10000, 0,
                                           SCE_KERNEL_CPU_MASK_USER_2, NULL);
    char line[64]; snprintf(line, sizeof(line), "audio thread=%d\n", g_audio_thread); diag_append(line);
    if (g_audio_thread >= 0) sceKernelStartThread(g_audio_thread, 0, NULL);
}

void native_audio_stop(void) { g_audio_running = 0; }

void native_music_playef(void *self, int id) {
    (void)self;
    native_audio_play_sfx(id);
}

void native_music_playef2(void *self, int id, int variant) {
    (void)self;
    (void)variant;
    native_audio_play_sfx(id);
}

void native_music_play(void *self, int song) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int start_ms = music_play_start_ms(song);
    int length_ms = ((unsigned)song < (sizeof(g_music_lengths_ms) / sizeof(g_music_lengths_ms[0]))) ? g_music_lengths_ms[song] : 0;
    g_music_song = song;
    g_music_requested_ms = start_ms;
    g_music_playing = 1;
    g_music_requested_song = song;
    g_music_stop_requested = 0;
    if (g_music_loaded_song == song && g_music.fd >= 0) {
        set_music_position_ms(start_ms);
        g_music_requested_song = -1;
    } else {
        g_music_clock_offset_ms = start_ms;
        g_music_clock_start_us = (uint64_t)sceKernelGetSystemTimeWide();
    }
    g_music_clock_running = 1;
    g_music_play_count++;
    g_music_section_resume_song = -1;
    sync_game_music_state(self, song, start_ms, 1);
    char line[144]; snprintf(line, sizeof(line), "music_clock play song=%d ms=%d count=%d length=%d caller=%p\n",
                             song, start_ms, g_music_play_count, length_ms, (void *)caller); diag_append(line);
}

void native_music_resume(void *self, int song, int ms) {
    g_music_song = song;
    g_music_requested_ms = ms;
    g_music_playing = 1;
    g_music_requested_song = song;
    g_music_stop_requested = 0;
    if (g_music_loaded_song == song && g_music.fd >= 0) {
        set_music_position_ms(ms);
        g_music_requested_song = -1;
    } else {
        g_music_clock_offset_ms = ms < 0 ? 0 : ms;
        g_music_clock_start_us = (uint64_t)sceKernelGetSystemTimeWide();
    }
    g_music_clock_running = 1;
    sync_game_music_state(self, song, ms, 1);
    char line[96]; snprintf(line, sizeof(line), "music_clock resume song=%d ms=%d\n", song, ms); diag_append(line);
}

void native_music_stop(void *self) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    g_music_clock_running = 0;
    g_music_playing = 0;
    g_music_requested_ms = -1;
    g_music_clock_offset_ms = 0;
    g_music_stop_requested = 1;
    g_music_requested_song = -1;
    g_music_section_resume_song = -1;
    sync_game_music_state(self, -1, 0, 0);
    char line[112]; snprintf(line, sizeof(line), "music_clock stop song=%d caller=%p\n", g_music_song, (void *)caller); diag_append(line);
}

void native_music_fadeout(void *self) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int resume_ms = 0;
    if (g_music_clock_running && g_music_clock_start_us) {
        uint64_t now_us = (uint64_t)sceKernelGetSystemTimeWide();
        resume_ms = g_music_clock_offset_ms + (int)((now_us - g_music_clock_start_us) / 1000);
    }
    g_music_clock_running = 0;
    g_music_playing = 0;
    g_music_requested_ms = -1;
    g_music_clock_offset_ms = 0;
    g_music_stop_requested = 1;
    g_music_requested_song = -1;
    g_music_section_resume_song = g_music_song;
    sync_game_music_state(self, -1, 0, 0);
    char line[128]; snprintf(line, sizeof(line), "music_clock fadeout song=%d resume_ms=%d caller=%p\n", g_music_song, resume_ms, (void *)caller); diag_append(line);
}

int native_music_get_current_song_ms(void *self) {
    (void)self;
    if (!g_music_clock_running || !g_music_clock_start_us) return 0;
    uint64_t now_us = (uint64_t)sceKernelGetSystemTimeWide();
    return g_music_clock_offset_ms + (int)((now_us - g_music_clock_start_us) / 1000);
}

int native_music_get_current_song_loaded(void *self) {
    (void)self;
    return g_music_clock_running && g_music_song >= 0 &&
           ((g_music_loaded_song == g_music_song && g_music.fd >= 0) || g_music_requested_song == g_music_song);
}

void native_audio_install_hooks(void *superhex_mod_ptr) {
    so_module *mod = (so_module *)superhex_mod_ptr;
    uintptr_t a;
    g_orig_loadsongdata = (void *)so_symbol(mod, "_ZN10musicclass12loadsongdataEi");
    if ((a = so_symbol(mod, "_ZN10musicclass4playEi"))) hook_addr(a, (uintptr_t)&native_music_play);
    if ((a = so_symbol(mod, "_ZN10musicclass6resumeEii"))) hook_addr(a, (uintptr_t)&native_music_resume);
    if ((a = so_symbol(mod, "_ZN10musicclass4stopEv"))) hook_addr(a, (uintptr_t)&native_music_stop);
    if ((a = so_symbol(mod, "_ZN10musicclass7fadeoutEv"))) hook_addr(a, (uintptr_t)&native_music_fadeout);
    if ((a = so_symbol(mod, "_ZN10musicclass16getCurrentSongMSEv"))) hook_addr(a, (uintptr_t)&native_music_get_current_song_ms);
    if ((a = so_symbol(mod, "_ZN10musicclass20getCurrentSongLoadedEv"))) hook_addr(a, (uintptr_t)&native_music_get_current_song_loaded);
    if ((a = so_symbol(mod, "_ZN10musicclass6playefEi"))) hook_addr(a, (uintptr_t)&native_music_playef);
    if ((a = so_symbol(mod, "_ZN10musicclass6playefEii"))) hook_addr(a, (uintptr_t)&native_music_playef2);
    l_info("native audio: installed SFX hooks");
}
