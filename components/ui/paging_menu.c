#include "paging_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static const char* TAG = "rv3.paging_menu";

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

struct paging_menu {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[MENU_BTN_NB];
	lv_obj_t *label[MENU_BTN_NB];
	int page_nb;
	int page_count;
	int item_nb;
	struct paging_cbs *client_cbs;
	void *ctx;
};

static inline struct paging_menu *get_paging_menu()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct paging_menu, cbs);
}

static void paging_menu_destroy(struct paging_menu *menu)
{
	if (menu->client_cbs->destroy)
		menu->client_cbs->destroy(menu->ctx);
	lv_obj_del(menu->scr);
	free(menu);
}

static void handle_back_event(struct paging_menu *menu)
{
	lv_disp_load_scr(menu->prev_scr);
	paging_menu_destroy(menu);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct paging_menu *menu = container_of(cbs, struct paging_menu, cbs);
	ui_hdl prev;

	if (menu->client_cbs->is_root && menu->client_cbs->is_root(menu->ctx)) {
		lv_disp_load_scr(menu->scr);
		return ;
	}

	prev = lv_obj_get_user_data(menu->prev_scr);
	prev->destroy_chained(prev);

	paging_menu_destroy(menu);
}

static enum menu_button get_btn_id_from_obj(struct paging_menu *menu, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < MENU_BTN_NB; i++) {
		if (menu->btn[i] == btn)
			return i;
	}

	return MENU_BTN_NB;
}

static void paging_menu_setup_page(struct paging_menu *menu, int page_nb)
{
	int page_start = page_nb * 3;
	int page_end = page_start + 3 < menu->item_nb ? page_start + 3 : menu->item_nb;

	enum menu_button btn_id = MENU_BTN_0;
	int i;

	menu->page_nb = page_nb;
	lv_obj_set_hidden(menu->btn[MENU_UP], page_nb == 0);
	lv_obj_set_hidden(menu->btn[MENU_DOWN], page_nb == menu->page_count - 1);

	for (i = page_start; i < page_end; i++, btn_id++) {
		char *item_name = menu->client_cbs->get_item_label(menu->ctx, i);

		lv_obj_set_hidden(menu->btn[btn_id], false);
		lv_label_set_text(menu->label[btn_id], item_name);
		if (menu->client_cbs->put_item_label)
			menu->client_cbs->put_item_label(menu->ctx, item_name);
	}
	for (i = page_end; i < page_start + 3; i++, btn_id++)
		lv_obj_set_hidden(menu->btn[btn_id], true);
}

static void paging_menu_event_long_pressed_cb(struct paging_menu *menu, enum menu_button btn_id)
{
	if (!menu->client_cbs->long_select_item)
		return;

	switch (btn_id) {
	case MENU_MENU:
	case MENU_BACK:
	case MENU_UP:
	case MENU_DOWN:
		break;
	case MENU_BTN_0:
	case MENU_BTN_1:
	case MENU_BTN_2:
		ESP_LOGI(TAG, "You select %s", lv_label_get_text(menu->label[btn_id]));
		menu->client_cbs->long_select_item(menu->ctx,
						   lv_label_get_text(menu->label[btn_id]),
						   menu->page_nb * 3 + btn_id - MENU_BTN_0);
		break;
	default:
		assert(0);
	}
}

static void paging_menu_event_clicked_cb(struct paging_menu *menu, enum menu_button btn_id)
{
	switch (btn_id) {
	case MENU_MENU:
		destroy_chained(&menu->cbs);
		break;
	case MENU_BACK:
		handle_back_event(menu);
		break;
	case MENU_UP:
		paging_menu_setup_page(menu, menu->page_nb - 1);
		break;
	case MENU_DOWN:
		paging_menu_setup_page(menu, menu->page_nb + 1);
		break;
	case MENU_BTN_0:
	case MENU_BTN_1:
	case MENU_BTN_2:
		ESP_LOGI(TAG, "You select %s", lv_label_get_text(menu->label[btn_id]));
		menu->client_cbs->select_item(menu->ctx,
					      lv_label_get_text(menu->label[btn_id]),
					      menu->page_nb * 3 + btn_id - MENU_BTN_0);
		break;
	default:
		assert(0);
	}
}

static void paging_menu_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct paging_menu *menu = get_paging_menu();
	enum menu_button btn_id = get_btn_id_from_obj(menu, btn);

	if (event != LV_EVENT_CLICKED && event !=LV_EVENT_LONG_PRESSED)
		return;

	if (event == LV_EVENT_LONG_PRESSED)
		paging_menu_event_long_pressed_cb(menu, btn_id);
	else
		paging_menu_event_clicked_cb(menu, btn_id);
}

static void setup_new_screen(struct paging_menu *menu)
{
	if (menu->scr)
		lv_obj_del(menu->scr);
	menu->scr = lv_obj_create(NULL, NULL);
	assert(menu->scr);
	lv_obj_set_user_data(menu->scr, &menu->cbs);
	lv_disp_load_scr(menu->scr);
	lv_obj_set_size(menu->scr, 320, 240);
}

static void setup_paging_menu(struct paging_menu *menu)
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
		lv_obj_set_event_cb(menu->btn[i], paging_menu_event_cb);
	}
	if (!menu->client_cbs->is_root || !menu->client_cbs->is_root(menu->ctx)) {
		lv_obj_set_hidden(menu->btn[MENU_MENU], false);
		lv_label_set_text(menu->label[MENU_MENU], "MENU");
		lv_obj_set_hidden(menu->btn[MENU_BACK], false);
	}
	lv_label_set_text(menu->label[MENU_BACK], "BACK");
	lv_label_set_text(menu->label[MENU_UP], "UP");
	lv_label_set_text(menu->label[MENU_DOWN], "DOWN");

	paging_menu_setup_page(menu, 0);
}

ui_hdl paging_menu_create(int item_nb, struct paging_cbs *cbs, void *ctx)
{
	struct paging_menu *menu;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	menu->item_nb = item_nb;
	menu->client_cbs = cbs;
	menu->ctx = ctx;
	menu->page_count = ((menu->item_nb  - 1) / 3) + 1;

	menu->prev_scr = lv_disp_get_scr_act(NULL);
	menu->cbs.destroy_chained = destroy_chained;
	setup_paging_menu(menu);

	return &menu->cbs;
}
