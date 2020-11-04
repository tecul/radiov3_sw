#ifndef __RADIO_PLAYER__
#define __RADIO_PLAYER__ 1

#include "ui.h"

ui_hdl radio_player_create(const char *radio_label, const char *url, const char *port_nb,
			   const char *path, int rate);

#endif