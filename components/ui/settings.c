#include "settings.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"
#include "calibration.h"

static const char* TAG = "rv3.settings";

typedef void (*cb)(lv_obj_t *scr, lv_event_t event);

enum ui_button {
	UI_BTN_CALIBRATION,
	UI_BTN_WIFI,
	UI_MENU,
	UI_BACK,
	UI_BTN_NB
};

struct settings_menu {
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[UI_BTN_NB];
	lv_obj_t *label[UI_BTN_NB];
};

static void calibration_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	calibration_start();
}

static void wifi_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	printf("You click on wifi button\n");
}

static void menu_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	/* FIXME should destroy back to top menu */
	settings_destroy(lv_obj_get_user_data(lv_disp_get_scr_act(NULL)));
}

static void back_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	settings_destroy(lv_obj_get_user_data(lv_disp_get_scr_act(NULL)));
}

void *settings_create()
{
	const int sizes[UI_BTN_NB][2] = {
		{120, 60}, {120, 60},
		{60, 60}, {60, 60}
	};
	const lv_point_t pos[UI_BTN_NB] = {
		{100, 15}, {100, 90},
		{20, 15}, {20, 165}
	};
	const char *labels[UI_BTN_NB] = {
		"touch screen", "wifi", "menu", "back"
	};
	const cb cbs[] = {
		calibration_event_cb, wifi_event_cb, menu_event_cb, back_event_cb
	};
	struct settings_menu *settings;
	unsigned int i;

	settings = malloc(sizeof(*settings));
	if (!settings)
		return NULL;

	settings->prev_scr = lv_disp_get_scr_act(NULL);
	settings->scr = lv_obj_create(NULL, NULL);
	assert(settings->scr);
	lv_obj_set_user_data(settings->scr, settings);
	lv_disp_load_scr(settings->scr);
	lv_obj_set_size(settings->scr, 320, 240);

	for (i = 0; i < UI_BTN_NB; i++) {
		settings->btn[i] = lv_btn_create(settings->scr, NULL);
		assert(settings->btn[i]);
		lv_obj_set_size(settings->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(settings->btn[i], pos[i].x, pos[i].y);
		settings->label[i] = lv_label_create(settings->btn[i], NULL);
		assert(settings->label[i]);
		lv_label_set_text(settings->label[i], labels[i]);
		lv_obj_set_event_cb(settings->btn[i], cbs[i]);
	}

	return settings;
}

void settings_destroy(void *hdl)
{
	struct settings_menu *settings = (struct settings_menu *) hdl;

	lv_disp_load_scr(settings->prev_scr);
	lv_obj_del(settings->scr);

	free(settings);
}
