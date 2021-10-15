#include "system_menu.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "wifi.h"
#include "sdcard.h"
#include "bluetooth.h"
#include "esp_sntp.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static ui_hdl instance;

struct system_menu {
	struct ui_cbs cbs;
	lv_obj_t *wifi_label;
	lv_obj_t *sd_card_label;
	lv_obj_t *bluetooth_label;
	lv_obj_t *time;
	lv_obj_t *user;
	lv_task_t *task_level;
	bool is_sntp_done;
	int toggle;
};

static void destroy_chained(struct ui_cbs *cbs)
{
	;/* nothing to done */
}

static void restore_event(struct ui_cbs *cbs)
{
	;/* nothing to done */
}

static lv_obj_t *create_notif_label(char *symbol, int x)
{
	lv_obj_t *res = lv_label_create(lv_layer_top(), NULL);

	assert(res);
	lv_label_set_text(res, symbol);
	lv_obj_set_x(res, x);
	lv_obj_set_hidden(res, true);

	return res;
}

static void task_level_cb(struct _lv_task_t *task)
{
	struct system_menu *system = task->user_data;
	char buf[16];
	struct tm timeinfo;
	time_t now;
	bool is_wifi_connected = wifi_is_connected();

	if (is_wifi_connected && !system->is_sntp_done) {
		sntp_setoperatingmode(SNTP_OPMODE_POLL);
		sntp_setservername(0, "pool.ntp.org");
		sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
		sntp_init();
		system->is_sntp_done = true;
	}

	lv_obj_set_hidden(system->wifi_label, !is_wifi_connected);
	lv_obj_set_hidden(system->sd_card_label, !sdcard_is_present());
	switch (bluetooth_get_state()) {
	case BLUETOOTH_DISABLE:
		lv_obj_set_hidden(system->bluetooth_label, 1);
		break;
	case BLUETOOTH_CONNECTED:
		lv_obj_set_hidden(system->bluetooth_label, 0);
		break;
	case BLUETOOTH_DISCOVERABLE:
		lv_obj_set_hidden(system->bluetooth_label, system->toggle);
		break;
	default:
		assert(0);
	}

	time(&now);
	localtime_r(&now, &timeinfo);
	snprintf(buf, sizeof(buf), "%2d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
	lv_label_set_text(system->time, buf);
	/*snprintf(buf, sizeof(buf), "%02d/%02d/%04d", timeinfo.tm_mday,
		 timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
	lv_label_set_text(system->date, buf);*/
	system->toggle = 1 - system->toggle;
}

ui_hdl system_menu_create()
{
	struct system_menu *system;

	if (instance)
		return instance;

	system = malloc(sizeof(*system));
	if (!system)
		return NULL;
	memset(system, 0, sizeof(*system));

	system->wifi_label = create_notif_label(LV_SYMBOL_WIFI, 299);
	system->sd_card_label = create_notif_label(LV_SYMBOL_SD_CARD, 286);
	system->bluetooth_label = create_notif_label(LV_SYMBOL_BLUETOOTH, 271);

	system->time = lv_label_create(lv_layer_top(), NULL);
	assert(system->time);
	lv_obj_align(system->time, lv_layer_top(), LV_ALIGN_IN_TOP_LEFT, 2, 0);
	lv_label_set_text(system->time, "");
	system->user = lv_label_create(lv_layer_top(), NULL);
	assert(system->user);
	lv_obj_align_origo(system->user, lv_layer_top(), LV_ALIGN_IN_TOP_MID, 0, 10);
	lv_label_set_text(system->user, "");

	/* Set time zone to Paris. Got it from :
	 * # tail -1 /usr/share/zoneinfo/Europe/Paris
	 */
	putenv("TZ=CET-1CEST,M3.5.0,M10.5.0/3");
	tzset();
	system->task_level = lv_task_create(task_level_cb, 500, LV_TASK_PRIO_LOW, system);
	assert(system->task_level);

	system->cbs.destroy_chained = destroy_chained;
	system->cbs.restore_event = restore_event;
	instance = &system->cbs;

	return instance;
}

void system_menu_set_user_label(const char *text)
{
	struct system_menu *system = container_of(instance, struct system_menu, cbs);

	if (!instance)
		return ;

	lv_label_set_text(system->user, text);
}
