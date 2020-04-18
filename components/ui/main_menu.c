#include "main_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "settings.h"
#include "radio_menu.h"
#include "system_menu.h"
#include "artist_menu.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static ui_hdl instance;

typedef void (*cb)(lv_obj_t *scr, lv_event_t event);

enum ui_button {
	UI_BTN_RADIO,
	UI_BTN_MUSIC,
	UI_BTN_SETTINGS,
	UI_BTN_NB
};

struct main_menu {
	struct ui_cbs cbs;
	lv_obj_t *scr;
	lv_obj_t *btn[UI_BTN_NB];
	lv_obj_t *label[UI_BTN_NB];
	ui_hdl system_menu;
};

static void radio_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	radio_menu_create();
}

static void music_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	artist_menu_create();
}

static void settings_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	settings_create();
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct main_menu *menu = container_of(cbs, struct main_menu, cbs);

	/* stop here and restore main menu screen */
	lv_disp_load_scr(menu->scr);
}

ui_hdl main_menu_create()
{
	const int sizes[UI_BTN_NB][2] = {
		{120, 55}, {120, 55}, {120, 55}
	};
	const lv_point_t pos[UI_BTN_NB] = {
		{100, 24}, {100, 96}, {100, 168}
	};
	const char *labels[UI_BTN_NB] = {
		"radio", "music", "settings"
	};
	const cb cbs[] = {
		radio_event_cb, music_event_cb, settings_event_cb
	};
	struct main_menu *menu;
	unsigned int i;

	if (instance)
		return instance;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;

	menu->scr = lv_obj_create(NULL, NULL);
	assert(menu->scr);
	lv_obj_set_user_data(menu->scr, &menu->cbs);
	lv_disp_load_scr(menu->scr);
	lv_obj_set_size(menu->scr, 320, 240);

	for (i = 0; i < UI_BTN_NB; i++) {
		menu->btn[i] = lv_btn_create(menu->scr, NULL);
		assert(menu->btn[i]);
		lv_obj_set_size(menu->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(menu->btn[i], pos[i].x, pos[i].y);
		menu->label[i] = lv_label_create(menu->btn[i], NULL);
		assert(menu->label[i]);
		lv_label_set_text(menu->label[i], labels[i]);
		lv_obj_set_event_cb(menu->btn[i], cbs[i]);
	}
	menu->cbs.destroy_chained = destroy_chained;
	instance = &menu->cbs;

	menu->system_menu = system_menu_create();

	return instance;
}
