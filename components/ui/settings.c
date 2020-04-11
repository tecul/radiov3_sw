#include "settings.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"
#include "calibration.h"
#include "wifi_setting.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

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
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[UI_BTN_NB];
	lv_obj_t *label[UI_BTN_NB];
};

static void settings_destroy(struct settings_menu *settings)
{
	lv_obj_del(settings->scr);
	free(settings);
}

static void settings_back(struct ui_cbs *cbs)
{
	struct settings_menu *settings = container_of(cbs, struct settings_menu, cbs);

	lv_disp_load_scr(settings->prev_scr);
	settings_destroy(settings);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct settings_menu *settings = container_of(cbs, struct settings_menu, cbs);
	ui_hdl prev;

	prev = lv_obj_get_user_data(settings->prev_scr);
	prev->destroy_chained(prev);

	settings_destroy(settings);
}

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
	wifi_setting_create();
}

static void menu_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	destroy_chained(lv_obj_get_user_data(lv_disp_get_scr_act(NULL)));
}

static void back_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	settings_back(lv_obj_get_user_data(lv_disp_get_scr_act(NULL)));
}

ui_hdl settings_create()
{
	const int sizes[UI_BTN_NB][2] = {
		{120, 55}, {120, 55},
		{60, 55}, {60, 55}
	};
	const lv_point_t pos[UI_BTN_NB] = {
		{100, 24}, {100, 96},
		{20, 24}, {20, 168}
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
	lv_obj_set_user_data(settings->scr, &settings->cbs);
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
	settings->cbs.destroy_chained = destroy_chained;

	return &settings->cbs;
}
