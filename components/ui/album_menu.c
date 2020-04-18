#include "album_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"

#include "db.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static const char* TAG = "rv3.album_menu";

enum menu_button {
	MENU_MENU,
	MENU_BACK,
	MENU_UP,
	MENU_DOWN,
	MENU_BTN_0,
	MENU_BTN_1,
	MENU_BTN_2,
	MENU_BTN_NB
};

struct album_menu {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[MENU_BTN_NB];
	lv_obj_t *label[MENU_BTN_NB];
	int page_nb;
	int page_count;
	void *db_hdl;
	int album_nb;
	char *artist;
};

static inline struct album_menu *get_album_menu()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct album_menu, cbs);
}

static void album_menu_destroy(struct album_menu *menu)
{
	lv_obj_del(menu->scr);
	free(menu);
}

static void handle_back_event(struct album_menu *menu)
{
	lv_disp_load_scr(menu->prev_scr);
	album_menu_destroy(menu);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct album_menu *menu = container_of(cbs, struct album_menu, cbs);
	ui_hdl prev;

	prev = lv_obj_get_user_data(menu->prev_scr);
	prev->destroy_chained(prev);

	album_menu_destroy(menu);
}

static enum menu_button get_btn_id_from_obj(struct album_menu *menu, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < MENU_BTN_NB; i++) {
		if (menu->btn[i] == btn)
			return i;
	}

	return MENU_BTN_NB;
}

static void album_menu_setup_page(struct album_menu *menu, int page_nb)
{
	int page_start = page_nb * 3;
	int page_end = page_start + 3 < menu->album_nb ? page_start + 3 : menu->album_nb;

	enum menu_button btn_id = MENU_BTN_0;
	int i;

	menu->page_nb = page_nb;
	lv_obj_set_hidden(menu->btn[MENU_UP], page_nb == 0);
	lv_obj_set_hidden(menu->btn[MENU_DOWN], page_nb == menu->page_count - 1);

	for (i = page_start; i < page_end; i++, btn_id++) {
		char *album_name = db_album_get(menu->db_hdl, menu->artist, i);

		lv_obj_set_hidden(menu->btn[btn_id], false);
		lv_label_set_text(menu->label[btn_id], album_name);
		db_put_item(menu->db_hdl, album_name);
	}
	for (i = page_end; i < page_start + 3; i++, btn_id++)
		lv_obj_set_hidden(menu->btn[btn_id], true);
}

static void album_menu_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct album_menu *menu = get_album_menu();
	enum menu_button btn_id = get_btn_id_from_obj(menu, btn);

	if (event != LV_EVENT_CLICKED)
		return;

	switch (btn_id) {
	case MENU_MENU:
		destroy_chained(&menu->cbs);
		break;
	case MENU_BACK:
		handle_back_event(menu);
		break;
	case MENU_UP:
		album_menu_setup_page(menu, menu->page_nb - 1);
		break;
	case MENU_DOWN:
		album_menu_setup_page(menu, menu->page_nb + 1);
		break;
	case MENU_BTN_0:
	case MENU_BTN_1:
	case MENU_BTN_2:
		ESP_LOGI(TAG, "You select %s", lv_label_get_text(menu->label[btn_id]));
		break;
	default:
		assert(0);
	}
}

static void setup_new_screen(struct album_menu *menu)
{
	if (menu->scr)
		lv_obj_del(menu->scr);
	menu->scr = lv_obj_create(NULL, NULL);
	assert(menu->scr);
	lv_obj_set_user_data(menu->scr, &menu->cbs);
	lv_disp_load_scr(menu->scr);
	lv_obj_set_size(menu->scr, 320, 240);
}

static void setup_album_menu(struct album_menu *menu)
{
	const int sizes[MENU_BTN_NB][2] = {
		{60, 55}, {60, 55}, {60, 55}, {60, 55},
		{120, 55}, {120, 55}, {120, 55}
	};
	const lv_point_t pos[MENU_BTN_NB] = {
		{20, 24}, {20, 168}, {240, 15}, {240, 168},
		{100, 24}, {100, 96}, {100, 168}
	};
	int i;

	setup_new_screen(menu);

	for (i = 0; i < MENU_BTN_NB; i++) {
		menu->btn[i] = lv_btn_create(menu->scr, NULL);
		assert(menu->btn[i]);
		lv_obj_set_size(menu->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(menu->btn[i], pos[i].x, pos[i].y);
		lv_obj_set_hidden(menu->btn[i], true);
		menu->label[i] = lv_label_create(menu->btn[i], NULL);
		assert(menu->label);
		lv_obj_set_event_cb(menu->btn[i], album_menu_event_cb);
	}
	lv_obj_set_hidden(menu->btn[MENU_MENU], false);
	lv_label_set_text(menu->label[MENU_MENU], "MENU");
	lv_obj_set_hidden(menu->btn[MENU_BACK], false);
	lv_label_set_text(menu->label[MENU_BACK], "BACK");
	lv_label_set_text(menu->label[MENU_UP], "UP");
	lv_label_set_text(menu->label[MENU_DOWN], "DOWN");

	album_menu_setup_page(menu, 0);
}

ui_hdl album_menu_create(void *db_hdl, char *artist)
{
	struct album_menu *menu;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	menu->db_hdl = db_hdl;
	menu->artist = artist;
	menu->album_nb = db_album_get_nb(menu->db_hdl, artist);
	menu->page_count = ((menu->album_nb  - 1) / 3) + 1;

	menu->prev_scr = lv_disp_get_scr_act(NULL);
	menu->cbs.destroy_chained = destroy_chained;
	setup_album_menu(menu);

	return &menu->cbs;
}
