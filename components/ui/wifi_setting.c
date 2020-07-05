#include "wifi_setting.h"
#include "wifi.h"

#include <stdlib.h>
#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "system_menu.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static const char* TAG = "rv3.settings.wifi";

enum kb_button {
	KB_LEFT,
	KB_CHAR,
	KB_RIGHT,
	KB_DELETE,
	KB_DONE,
	KB_BTN_NB
};

typedef void (*cb)(lv_obj_t *scr, lv_event_t event);

struct wifi_setting_menu {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *label_status;
	lv_obj_t *btn[3];
	lv_obj_t *label[3];
	lv_obj_t *ta;
	lv_obj_t *kb_btn[KB_BTN_NB];
	lv_obj_t *kb_label[KB_BTN_NB];
	char kb_current[2];
	char *ssid;
};

static inline struct wifi_setting_menu *get_wifi_setting()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct wifi_setting_menu, cbs);
}

static void wifi_leave(struct wifi_setting_menu *wifi)
{
	ui_hdl prev = lv_obj_get_user_data(wifi->prev_scr);

	lv_disp_load_scr(wifi->prev_scr);
	if (prev->restore_event)
		prev->restore_event(prev);

	lv_obj_del(wifi->scr);
	if (wifi->ssid)
		free(wifi->ssid);
	free(wifi);
}

static enum kb_button get_btn_id_from_obj(struct wifi_setting_menu *wifi, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < KB_BTN_NB; i++) {
		if (wifi->kb_btn[i] == btn)
			return i;
	}

	return KB_BTN_NB;
}

static void kb_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct wifi_setting_menu *wifi = get_wifi_setting();
	enum kb_button btn_id = get_btn_id_from_obj(wifi, btn);
	int ret;

	if( event != LV_EVENT_CLICKED && event != LV_EVENT_LONG_PRESSED_REPEAT)
		return;
	if (event == LV_EVENT_LONG_PRESSED_REPEAT && btn_id != KB_LEFT && btn_id != KB_RIGHT)
		return;

	switch (btn_id) {
	case KB_LEFT:
		wifi->kb_current[0]--;
		if (wifi->kb_current[0] < 0x20)
			wifi->kb_current[0] = 0x7E;
		lv_label_set_text(wifi->kb_label[KB_CHAR], wifi->kb_current);
		break;
	case KB_RIGHT:
		wifi->kb_current[0]++;
		if (wifi->kb_current[0] >0x7E)
			wifi->kb_current[0] = 0x20;
		lv_label_set_text(wifi->kb_label[KB_CHAR], wifi->kb_current);
		break;
	case KB_CHAR:
		lv_ta_add_char(wifi->ta, wifi->kb_current[0]);
		break;
	case KB_DELETE:
		lv_ta_del_char(wifi->ta);
		break;
	case KB_DONE:
		printf("ssid = %s\n", wifi->ssid);
		printf("passwd = %s\n", lv_ta_get_text(wifi->ta));
		ret = wifi_set_credentials(wifi->ssid, (char *) lv_ta_get_text(wifi->ta));
		if (!ret)
			wifi_connect_start();

		wifi_leave(wifi);
		break;
	default:
		assert(0);
	}
}

static int sort_by_rssi(const void *a, const void *b)
{
	const wifi_ap_record_t *aa = a;
	const wifi_ap_record_t *bb = b;

	if (aa->rssi == bb->rssi)
		return 0;

	if (aa->rssi > bb->rssi)
		return -1;

	return 1;
}

static void setup_new_screen(struct wifi_setting_menu *wifi)
{
	if (wifi->scr)
		lv_obj_del(wifi->scr);
	wifi->scr = lv_obj_create(NULL, NULL);
	assert(wifi->scr);
	lv_obj_set_user_data(wifi->scr, &wifi->cbs);
	lv_disp_load_scr(wifi->scr);
	lv_obj_set_size(wifi->scr, 320, 240);
}

static void setup_pwd_screen(struct wifi_setting_menu *wifi)
{
	const int sizes[KB_BTN_NB][2] = {
		{60, 55}, {60, 55}, {60, 55}, {60, 55}, {120, 55}
	};
	const lv_point_t pos[KB_BTN_NB] = {
		{10, 80}, {130, 80}, {250, 80},
		{10, 170}, {130, 170}
	};
	const char *labels[KB_BTN_NB] = {
		"<", "a", ">", "del", "done"
	};
	int i;

	setup_new_screen(wifi);

	wifi->ta = lv_ta_create(wifi->scr, NULL);
	lv_ta_set_one_line(wifi->ta, true);
	lv_obj_align(wifi->ta, NULL, LV_ALIGN_IN_TOP_MID, 0, 10);
	lv_obj_set_y(wifi->ta, 24);
	lv_ta_set_text(wifi->ta, "");

	wifi->kb_current[0] = labels[KB_CHAR][0];
	wifi->kb_current[1] = '\0';
	for (i = 0; i < KB_BTN_NB; i++) {
		wifi->kb_btn[i] = lv_btn_create(wifi->scr, NULL);
		assert(wifi->kb_btn[i]);
		lv_obj_set_size(wifi->kb_btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(wifi->kb_btn[i], pos[i].x, pos[i].y);
		wifi->kb_label[i] = lv_label_create(wifi->kb_btn[i], NULL);
		assert(wifi->kb_label[i]);
		lv_label_set_text(wifi->kb_label[i], labels[i]);
		lv_obj_set_event_cb(wifi->kb_btn[i], kb_event_cb);
	}
}

static void select_ssid_cb(lv_obj_t *btn, lv_event_t event)
{
	struct wifi_setting_menu *wifi = get_wifi_setting();
	lv_obj_t *label = lv_obj_get_child(btn, NULL);

	if( event != LV_EVENT_CLICKED)
		return;

	if (!label) {
		ESP_LOGE(TAG, "label null for %p", btn);
		wifi_leave(wifi);
	}
	wifi->ssid = strdup(lv_label_get_text(label));

	setup_pwd_screen(wifi);
}

static void setup_ssid_screen(struct wifi_setting_menu *wifi,
	wifi_ap_record_t *ap_list_buffer, uint16_t ap_nb)
{
	const int sizes[3][2] = {
		{120, 55}, {120, 55}, {120, 55}
	};
	const lv_point_t pos[3] = {
		{100, 24}, {100, 96}, {100, 168}
	};
	int i;

	setup_new_screen(wifi);
	for (i = 0; i < ap_nb; i++) {
		wifi->btn[i] = lv_btn_create(wifi->scr, NULL);
		assert(wifi->btn[i]);
		lv_obj_set_size(wifi->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(wifi->btn[i], pos[i].x, pos[i].y);
		wifi->label[i] = lv_label_create(wifi->btn[i], NULL);
		assert(wifi->label[i]);
		lv_label_set_text(wifi->label[i], (const char *) ap_list_buffer[i].ssid);
		lv_obj_set_event_cb(wifi->btn[i], select_ssid_cb);
	}
}

static void wifi_scan_done_cb(void *args)
{
	struct wifi_setting_menu *wifi = args;
	wifi_ap_record_t *ap_list_buffer;
	uint16_t ap_nb;
	esp_err_t ret;

	ret = esp_wifi_scan_get_ap_num(&ap_nb);
	if (ret)
		wifi_leave(wifi);

	ap_list_buffer = malloc(ap_nb * sizeof(wifi_ap_record_t));
	if (!ap_list_buffer)
		wifi_leave(wifi);

	ret = esp_wifi_scan_get_ap_records(&ap_nb, ap_list_buffer);
	if (ret) {
		free(ap_list_buffer);
		wifi_leave(wifi);
	}

	/* sort by rssi and only keep 3 ap max */
	qsort(ap_list_buffer, ap_nb, sizeof(wifi_ap_record_t), sort_by_rssi);
	ap_nb = ap_nb > 3 ? 3 : ap_nb;

	setup_ssid_screen(wifi, ap_list_buffer, ap_nb);

	free(ap_list_buffer);
}

static void setup_welcome_screen(struct wifi_setting_menu *wifi)
{
	setup_new_screen(wifi);

	wifi->label_status = lv_label_create(lv_disp_get_scr_act(NULL), NULL);
	assert(wifi->label_status);
	lv_label_set_align(wifi->label_status, LV_LABEL_ALIGN_CENTER);
	lv_label_set_text(wifi->label_status, "wifi scanning ....");
	lv_obj_set_pos(wifi->label_status,
		       (320 - lv_obj_get_width(wifi->label_status)) / 2,
		       (240 - lv_obj_get_height(wifi->label_status)) / 2);
	wifi_scan_start(wifi_scan_done_cb, wifi);
}

ui_hdl wifi_setting_create()
{
	struct wifi_setting_menu *wifi;

	wifi = malloc(sizeof(*wifi));
	if (!wifi)
		return NULL;
	memset(wifi, 0, sizeof(*wifi));

	wifi->prev_scr = lv_disp_get_scr_act(NULL);
	wifi->cbs.destroy_chained = NULL;
	setup_welcome_screen(wifi);
	system_menu_set_user_label("");

	return &wifi->cbs;
}
