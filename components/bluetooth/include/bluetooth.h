#ifndef __BLUETOOTH__
#define __BLUETOOTH__ 1

#include <stdint.h>
/* FIXME : tmp until webring back audio stack */
#if 0
#include "audio.h"
#else
typedef void (*audio_track_info_cb)(void *hdl, char *track_title);
#endif

typedef void (*bt_audio_cfg_cb)(int sample_rate);
typedef void (*bt_audio_data_cb)(const uint8_t *data, uint32_t len);

#define BLUETOOTH_DISABLE	0
#define BLUETOOTH_DISCOVERABLE	1
#define BLUETOOTH_CONNECTED	2

int bluetooth_init(char *name);
int bluetooth_enable(bt_audio_cfg_cb audio_cfg_cb, bt_audio_data_cb audio_data_cb, void *hdl_cb, audio_track_info_cb track_info_cb);
int bluetooth_disable(void);
int bluetooth_cmd_next(void);
int bluetooth_get_state(void);

#endif