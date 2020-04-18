#include "settings.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"
#include "calibration.h"
#include "wifi_setting.h"
#include "db.h"
#include "paging_menu.h"

#define ARRAY_SIZE(a)		(sizeof(a)/sizeof(a[0]))

static const char* TAG = "rv3.settings";

const char *settings_labels[] = {"touch screen", "wifi", "database update"};

static char *settings_get_item_label(void *ctx, int index)
{
	return (char *) settings_labels[index];
}

static void settings_select_item(void *ctx, char *selected_label, int index)
{
	ESP_LOGI(TAG, "select index %d\n", index);

	switch (index) {
	case 0:
		calibration_start();
		break;
	case 1:
		wifi_setting_create();
		break;
	case 2:
		update_db("/sdcard/music.db", "/sdcard/Music");
		break;
	default:
		ESP_LOGE(TAG, "unknown index %d", index);
	}
}

static struct paging_cbs cbs = {
	.get_item_label = settings_get_item_label,
	.select_item = settings_select_item,
};

ui_hdl settings_create()
{
	return paging_menu_create(ARRAY_SIZE(settings_labels), &cbs, NULL);
}
