#include "main_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"
#include "settings.h"
#include "radio_menu.h"
#include "system_menu.h"
#include "artist_menu.h"
#include "playlist_menu.h"

#include "paging_menu.h"

#define ARRAY_SIZE(a)		(sizeof(a)/sizeof(a[0]))

static const char* TAG = "rv3.main";

const char *main_labels[] = {"radio", "music", "playlist", "settings"};

static char *main_get_item_label(void *ctx, int index)
{
	return (char *) main_labels[index];
}

static void main_select_item(void *ctx, char *selected_label, int index)
{
	ESP_LOGI(TAG, "select index %d\n", index);

	switch (index) {
	case 0:
		radio_menu_create();
		break;
	case 1:
		artist_menu_create();
		break;
	case 2:
		playlist_menu_create("/sdcard/playlist");
		break;
	case 3:
		settings_create();
		break;
	default:
		ESP_LOGE(TAG, "unknown index %d", index);
	}
}

static int main_is_root(void *ctx)
{
	return 1;
}

static struct paging_cbs cbs = {
	.get_item_label = main_get_item_label,
	.select_item = main_select_item,
	.is_root = main_is_root,
};

ui_hdl main_menu_create()
{
	system_menu_create();
	return paging_menu_create(ARRAY_SIZE(main_labels), &cbs, NULL);
}
