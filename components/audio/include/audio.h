#ifndef __AUDIO__
#define __AUDIO__ 1

void audio_init(void);
void audio_radio_play(char *url, char *port_nb, char *path);
void audio_radio_stop(void);
void audio_music_play(char *filepath);
void audio_music_stop(void);
int audio_sound_level_up(void);
int audio_sound_level_down(void);
int audio_sound_get_level(void);
int audio_sound_get_max_level(void);
int audio_buffer_level(void);

#endif