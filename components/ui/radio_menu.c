#include "radio_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"

#include "radio_player.h"
#include "wifi.h"

#define ARRAY_SIZE(a)		(sizeof(a)/sizeof(a[0]))

const char *radio_labels[] = {"RADIO ISA", "RTL2", "FRANCE INFO", "EUROPE 1",
			      "FRANCE INTER"};
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

#define RADIO_PAGE_NB		(((ARRAY_SIZE(radio_labels) - 1) / 3) + 1)

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

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static const char* TAG = "rv3.radio_menu";

struct radio_menu {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[MENU_BTN_NB];
	lv_obj_t *label[MENU_BTN_NB];
	int page_nb;
};

static inline struct radio_menu *get_radio_menu()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct radio_menu, cbs);
}

static void radio_menu_destroy(struct radio_menu *radio)
{
	lv_obj_del(radio->scr);
	free(radio);
}

static void handle_back_event(struct radio_menu *radio)
{
	lv_disp_load_scr(radio->prev_scr);
	radio_menu_destroy(radio);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct radio_menu *radio = container_of(cbs, struct radio_menu, cbs);
	ui_hdl prev;

	prev = lv_obj_get_user_data(radio->prev_scr);
	prev->destroy_chained(prev);

	radio_menu_destroy(radio);
}

static void radio_menu_setup_page(struct radio_menu *radio, int page_nb)
{
	int page_start = page_nb * 3;
	int page_end = page_start + 3 < ARRAY_SIZE(radio_labels) ?
				    page_start + 3 : ARRAY_SIZE(radio_labels);
	enum menu_button btn_id = MENU_BTN_0;
	int i;

	radio->page_nb = page_nb;
	lv_obj_set_hidden(radio->btn[MENU_UP], page_nb == 0);
	lv_obj_set_hidden(radio->btn[MENU_DOWN], page_nb == RADIO_PAGE_NB - 1);

	for (i = page_start; i < page_end; i++, btn_id++) {
		lv_obj_set_hidden(radio->btn[btn_id], false);
		lv_label_set_text(radio->label[btn_id], radio_labels[btn_id - MENU_BTN_0 + page_start]);
	}
	for (i = page_end; i < page_start + 3; i++, btn_id++)
		lv_obj_set_hidden(radio->btn[btn_id], true);
}

static void handle_radio_select_event(struct radio_menu *radio, int index)
{
	int radio_nb = radio->page_nb * 3 + index;

	if (!wifi_is_connected()) {
		ESP_LOGW(TAG, "wifi not yet connected");
		return ;
	}

	ESP_LOGI(TAG, "select radio %s\n", radio_labels[radio_nb]);
	radio_player_create(radio_labels[radio_nb], radio_urls[radio_nb].url,
			    radio_urls[radio_nb].port_nb, radio_urls[radio_nb].path);
}

static enum menu_button get_btn_id_from_obj(struct radio_menu *radio, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < MENU_BTN_NB; i++) {
		if (radio->btn[i] == btn)
			return i;
	}

	return MENU_BTN_NB;
}

static void radio_menu_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct radio_menu *radio = get_radio_menu();
	enum menu_button btn_id = get_btn_id_from_obj(radio, btn);

	if (event != LV_EVENT_CLICKED)
		return;

	switch (btn_id) {
	case MENU_MENU:
		destroy_chained(&radio->cbs);
		break;
	case MENU_BACK:
		handle_back_event(radio);
		break;
	case MENU_UP:
		radio_menu_setup_page(radio, radio->page_nb - 1);
		break;
	case MENU_DOWN:
		radio_menu_setup_page(radio, radio->page_nb + 1);
		break;
	case MENU_BTN_0:
	case MENU_BTN_1:
	case MENU_BTN_2:
		handle_radio_select_event(radio, btn_id - MENU_BTN_0);
		break;
	default:
		assert(0);
	}
}

static void setup_new_screen(struct radio_menu *radio)
{
	if (radio->scr)
		lv_obj_del(radio->scr);
	radio->scr = lv_obj_create(NULL, NULL);
	assert(radio->scr);
	lv_obj_set_user_data(radio->scr, &radio->cbs);
	lv_disp_load_scr(radio->scr);
	lv_obj_set_size(radio->scr, 320, 240);
}

static void setup_radio_menu(struct radio_menu *radio)
{
	const int sizes[MENU_BTN_NB][2] = {
		{60, 60}, {60, 60}, {60, 60}, {60, 60},
		{120, 60}, {120, 60}, {120, 60}
	};
	const lv_point_t pos[MENU_BTN_NB] = {
		{20, 15}, {20, 165}, {240, 15}, {240, 165},
		{100, 15}, {100, 90}, {100, 165}
	};
	int i;

	setup_new_screen(radio);

	for (i = 0; i < MENU_BTN_NB; i++) {
		radio->btn[i] = lv_btn_create(radio->scr, NULL);
		assert(radio->btn[i]);
		lv_obj_set_size(radio->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(radio->btn[i], pos[i].x, pos[i].y);
		lv_obj_set_hidden(radio->btn[i], true);
		radio->label[i] = lv_label_create(radio->btn[i], NULL);
		assert(radio->label);
		lv_obj_set_event_cb(radio->btn[i], radio_menu_event_cb);
	}
	lv_obj_set_hidden(radio->btn[MENU_MENU], false);
	lv_label_set_text(radio->label[MENU_MENU], "MENU");
	lv_obj_set_hidden(radio->btn[MENU_BACK], false);
	lv_label_set_text(radio->label[MENU_BACK], "BACK");
	lv_label_set_text(radio->label[MENU_UP], "UP");
	lv_label_set_text(radio->label[MENU_DOWN], "DOWN");

	radio_menu_setup_page(radio, 0);
}

ui_hdl radio_menu_create()
{
	struct radio_menu *radio;

	radio = malloc(sizeof(*radio));
	if (!radio)
		return NULL;
	memset(radio, 0, sizeof(*radio));

	radio->prev_scr = lv_disp_get_scr_act(NULL);
	radio->cbs.destroy_chained = destroy_chained;
	setup_radio_menu(radio);

	return &radio->cbs;
}
