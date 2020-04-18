#include "radio_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"

#include "radio_player.h"
#include "wifi.h"
#include "paging_menu.h"

#define ARRAY_SIZE(a)		(sizeof(a)/sizeof(a[0]))

static const char* TAG = "rv3.radio_menu";
static struct {
	const char *url;
	const char *port_nb;
	const char *path;
} radio_urls[] = {
	{"radioisavoiron.ice.infomaniak.ch","80","/radioisavoiron-128.mp3"},
	{"streaming.radio.rtl2.fr","80","/rtl2-1-44-128"},
	{"icecast.radiofrance.fr","80","/franceinfo-midfi.mp3"},
	{"e1-live-mp3-128.scdn.arkena.com","80","/europe1.mp3"},
	{"icecast.radiofrance.fr","80","/franceinter-midfi.mp3"},
};

const char *radio_labels[] = {"RADIO ISA", "RTL2", "FRANCE INFO", "EUROPE 1",
			      "FRANCE INTER"};

static char *radio_get_item_label(void *ctx, int index)
{
	return (char *) radio_labels[index];
}

static void radio_select_item(void *ctx, char *selected_label, int index)
{
	if (!wifi_is_connected()) {
		ESP_LOGW(TAG, "wifi not yet connected");
		return ;
	}

	ESP_LOGI(TAG, "select radio %s\n", radio_labels[index]);
	radio_player_create(radio_labels[index], radio_urls[index].url,
			    radio_urls[index].port_nb, radio_urls[index].path);
}

static struct paging_cbs cbs = {
	.get_item_label = radio_get_item_label,
	.select_item = radio_select_item,
};

ui_hdl radio_menu_create()
{
	return paging_menu_create(ARRAY_SIZE(radio_labels), &cbs, NULL);
}
