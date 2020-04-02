#include "main_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "settings.h"

typedef void (*cb)(lv_obj_t *scr, lv_event_t event);

enum ui_button {
	UI_BTN_RADIO,
	UI_BTN_MUSIC,
	UI_BTN_SETTINGS,
	UI_BTN_NB
};

struct main_menu {
	lv_obj_t *scr;
	lv_obj_t *btn[UI_BTN_NB];
	lv_obj_t *label[UI_BTN_NB];
};

static void radio_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	printf("You click on radio button\n");
}

static void music_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	printf("You click on music button\n");
}

static void settings_event_cb(lv_obj_t *scr, lv_event_t event)
{
	if( event != LV_EVENT_CLICKED)
		return;

	settings_create();
}

void *main_menu_create()
{
	const int sizes[UI_BTN_NB][2] = {
		{120, 60}, {120, 60}, {120, 60}
	};
	const lv_point_t pos[UI_BTN_NB] = {
		{100, 15}, {100, 90}, {100, 165}
	};
	const char *labels[UI_BTN_NB] = {
		"radio", "music", "settings"
	};
	const cb cbs[] = {
		radio_event_cb, music_event_cb, settings_event_cb
	};
	struct main_menu *menu;
	unsigned int i;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;

	menu->scr = lv_obj_create(NULL, NULL);
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

	return menu;
}
