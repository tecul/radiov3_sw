#include "settings.h"

#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"
#include "calibration.h"
#include "wifi_setting.h"
#include "db.h"
#include "paging_menu.h"
#include "web_server.h"
#include "wifi.h"
#include "ota_update.h"
#include "esp_ota_ops.h"

#define ARRAY_SIZE(a)		(sizeof(a)/sizeof(a[0]))

static const char* TAG = "rv3.settings";

static const char *btns[] ={"Stop web server", ""};
static const char *btns_about[] ={"Exit", ""};
static lv_style_t modal_style;
static lv_obj_t *mbox;

static ui_hdl current;
/* log output */
#define CONSOLE_SERIAL		0
#define CONSOLE_NETWORK		1
#define CONSOLE_BUFFER_LEN	1024

static int console_type = CONSOLE_SERIAL;
static int console_socket;
static struct sockaddr_in console_address;
static char *console_buffer;

static void web_server_event_handler(lv_obj_t * obj, lv_event_t event)
{
	if (event == LV_EVENT_DELETE && obj == mbox) {
		lv_obj_del_async(lv_obj_get_parent(mbox));
		mbox = NULL;
	} else if (event == LV_EVENT_VALUE_CHANGED) {
		web_server_stop();
		lv_mbox_start_auto_close(mbox, 0);
	}
}

static void display_web_server_message()
{
	char msg[128];
	lv_obj_t *obj;

	lv_style_copy(&modal_style, &lv_style_plain_color);
	modal_style.body.main_color = modal_style.body.grad_color = LV_COLOR_BLACK;
	modal_style.body.opa = LV_OPA_50;

	obj = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(obj, &modal_style);
	lv_obj_set_pos(obj, 0, 0);
	lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);

	mbox = lv_mbox_create(obj, NULL);
	lv_obj_set_width(mbox, LV_HOR_RES - 40);
	snprintf(msg, sizeof(msg), "Connect to %s:8000", wifi_get_ip());
	lv_mbox_set_text(mbox, msg);
	lv_mbox_add_btns(mbox, btns);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_event_cb(mbox, web_server_event_handler);
}

static void ota_update_event_handler(lv_obj_t * obj, lv_event_t event)
{
	if (event == LV_EVENT_DELETE && obj == mbox) {
		lv_obj_del_async(lv_obj_get_parent(mbox));
		mbox = NULL;
	}
}

static void display_ota_update_message()
{
	lv_obj_t *obj;

	lv_style_copy(&modal_style, &lv_style_plain_color);
	modal_style.body.main_color = modal_style.body.grad_color = LV_COLOR_BLACK;
	modal_style.body.opa = LV_OPA_50;

	obj = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(obj, &modal_style);
	lv_obj_set_pos(obj, 0, 0);
	lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);

	mbox = lv_mbox_create(obj, NULL);
	lv_obj_set_width(mbox, LV_HOR_RES - 40);
	lv_mbox_set_text(mbox, "Checking update ...");
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_event_cb(mbox, ota_update_event_handler);
}

static void about_event_handler(lv_obj_t * obj, lv_event_t event)
{
	if (event == LV_EVENT_DELETE && obj == mbox) {
		lv_obj_del_async(lv_obj_get_parent(mbox));
		mbox = NULL;
	} else if (event == LV_EVENT_VALUE_CHANGED) {
		lv_mbox_start_auto_close(mbox, 0);
	}
}

static void display_about_message()
{
	char msg[128];
	lv_obj_t *obj;
	const esp_app_desc_t *app_desc = esp_ota_get_app_description();

	lv_style_copy(&modal_style, &lv_style_plain_color);
	modal_style.body.main_color = modal_style.body.grad_color = LV_COLOR_BLACK;
	modal_style.body.opa = LV_OPA_50;

	obj = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(obj, &modal_style);
	lv_obj_set_pos(obj, 0, 0);
	lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);

	mbox = lv_mbox_create(obj, NULL);
	lv_obj_set_width(mbox, LV_HOR_RES - 40);
	snprintf(msg, sizeof(msg), "%s", app_desc->version);
	lv_mbox_set_text(mbox, msg);
	lv_mbox_add_btns(mbox, btns_about);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_event_cb(mbox, about_event_handler);
}

static int vnetwork(const char *format, va_list va)
{
	int res = vsnprintf(console_buffer, CONSOLE_BUFFER_LEN, format, va);

	sendto(console_socket, console_buffer, strlen(console_buffer), 0, (struct sockaddr *) &console_address, sizeof(console_address));

	return res;
}

static int console_serial()
{
	ESP_LOGI(TAG, "Will switch to serial console");
	esp_log_set_vprintf(&vprintf);
	ESP_LOGI(TAG, "Switch to serial console done");
	if (console_socket) {
		close(console_socket);
		console_socket = 0;
		free(console_buffer);
		console_buffer = NULL;
	}

	return CONSOLE_SERIAL;
}

static int console_network()
{
	if (!wifi_is_connected())
		return CONSOLE_SERIAL;

	console_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (!console_socket)
		return CONSOLE_SERIAL;

	console_buffer = malloc(CONSOLE_BUFFER_LEN);
	if (!console_buffer) {
		close(console_socket);
		console_socket = 0;
		return CONSOLE_SERIAL;
	}

	memset(&console_address, 0, sizeof(struct sockaddr_in));
	console_address.sin_family = AF_INET;
	console_address.sin_port = htons(8888);
	console_address.sin_addr.s_addr = INADDR_BROADCAST;

	ESP_LOGI(TAG, "Will switch to network console");
	ESP_LOGI(TAG, "socat -u udp-recv:8888,reuseaddr -");
	esp_log_set_vprintf(&vnetwork);
	ESP_LOGI(TAG, "Switch to network console done");

	return CONSOLE_NETWORK;
}

static void console_toggle()
{
	console_type = console_type == CONSOLE_NETWORK ? console_serial() : console_network();
}

const char *settings_labels[] = {"start web server", "database update", "fw update",
	"wifi", "touch screen", "about"};

static char *settings_get_item_label(void *ctx, int index)
{
	if (index == 6)
		return console_type == CONSOLE_NETWORK ? "network console" : "serial console";

	return (char *) settings_labels[index];
}

static void settings_select_item(void *ctx, char *selected_label, int index)
{
	ESP_LOGI(TAG, "select index %d\n", index);

	switch (index) {
	case 6:
		console_toggle();
		paging_menu_refresh(current);
		break;
	case 5:
		display_about_message();
		break;
	case 4:
		calibration_start();
		break;
	case 3:
		wifi_setting_create();
		break;
	case 2:
		if (!wifi_is_connected())
			break;
		display_ota_update_message();
		ota_update_start(mbox);
		break;
	case 1:
		update_db("/sdcard/music.db", "/sdcard/Music");
		break;
	case 0:
		if (!wifi_is_connected())
			break;
		web_server_start();
		display_web_server_message();
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
	ui_hdl res;

	res = paging_menu_create(ARRAY_SIZE(settings_labels) + 1, &cbs, NULL);
	current = res;

	return res;
}
