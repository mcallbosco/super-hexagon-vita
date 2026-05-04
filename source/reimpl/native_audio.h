#ifndef SHX_NATIVE_AUDIO_H
#define SHX_NATIVE_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

void native_audio_install_hooks(void *superhex_mod);
void native_audio_start(void);
void native_audio_stop(void);
void native_audio_play_sfx(int id);
void native_music_playef(void *self, int id);
void native_music_playef2(void *self, int id, int variant);

#ifdef __cplusplus
}
#endif

#endif
