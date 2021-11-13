#ifndef __AUDIO__
#define __AUDIO__ 1

typedef void (*audio_track_info_cb)(void *hdl, char *track_title);

void audio_init(void);
void audio_radio_play(char *url, char *port_nb, char *path, int rate, int meta,
		      void *hdl, audio_track_info_cb track_info_cb);
void audio_radio_stop(void);
void audio_music_play(char *filepath);
void audio_music_stop(void);
int audio_sound_level_up(void);
int audio_sound_level_down(void);
int audio_sound_get_level(void);
int audio_sound_get_max_level(void);
int audio_buffer_level(void);
void audio_bluetooth_play(void *hdl, audio_track_info_cb track_info_cb);
void audio_bluetooth_stop(void);
void audio_bluetooth_next(void);

#endif