#include "calibration.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>

#include "lvgl.h"
#include "xpt2046.h"

#include "nvs_flash.h"
#include "esp_log.h"

static const char* TAG = "rv3.calibration";

#define TOUCH_NUMBER		3
#define CIRCLE_SIZE		20
#define CIRCLE_OFFSET		20

enum calib_state {
	CALIB_STATE_UPPER_LEFT,
	CALIB_STATE_BOTTOM_RIGHT,
	CALIB_STATE_WAIT_LEAVE,
};

struct calib {
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_coord_t hres;
	lv_coord_t vres;
	lv_obj_t *btn;
	lv_obj_t *label_msg;
	lv_obj_t *circle;
	lv_style_t style_circ;
	enum calib_state state;
	int counter;
	lv_point_t avr[TOUCH_NUMBER];
	lv_point_t point[2];
};

static float ax = 1.0;
static float bx = 0;
static float ay = 1.0;
static float by = 0;
static bool (*read_cb_save)(struct _lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

static esp_err_t nvs_set_float(nvs_handle handle, const char *key, float value)
{
	union {
		float f;
		uint32_t u;
	} data = { .f = value };

	return nvs_set_u32(handle, key, data.u);
}

static esp_err_t nvs_get_float(nvs_handle handle, const char *key, float *value)
{
	esp_err_t err;
	union {
		float f;
		uint32_t u;
	} data;

	err = nvs_get_u32(handle, key, &data.u);
	if (err)
		return err;
	*value = data.f;

	return ESP_OK;
}

static int apply_calibration(int x, float a, float b, int max_value)
{
	float res;

	res = a * x + b;
	res = res < 0 ? 0 : res;
	res = res > max_value ? max_value : res;

	return res;
}

static bool read_cb_calibrate(struct _lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
	bool res = read_cb_save(indev_drv, data);

	data->point.x = apply_calibration(data->point.x, ax, bx, 319);
	data->point.y = apply_calibration(data->point.y, ay, by, 239);

	return res;
}

static void get_avr_value(struct calib *calib, lv_point_t *p)
{
	int32_t x_sum = 0;
	int32_t y_sum = 0;
	uint8_t i;

	for (i = 0; i < TOUCH_NUMBER ; i++) {
		x_sum += calib->avr[i].x;
		y_sum += calib->avr[i].y;
	}
	p->x = x_sum / TOUCH_NUMBER;
	p->y = y_sum / TOUCH_NUMBER;
}

static void printf_label(struct calib *calib, char *msg, ...)
{
	char buf[64];
	va_list ap;

	va_start(ap, msg);
	vsnprintf(buf,sizeof(buf), msg, ap);
	va_end(ap);

	lv_label_set_text(calib->label_msg, buf);
	lv_obj_set_pos(calib->label_msg, (calib->hres - lv_obj_get_width(calib->label_msg)) / 2,
		       (calib->vres - lv_obj_get_height(calib->label_msg)) / 2);
}

static enum calib_state handle_state_upper_left(struct calib *calib)
{
	lv_indev_t * indev = lv_indev_get_act();
	char buf[64];

	lv_indev_get_point(indev, &calib->avr[calib->counter]);

	if (!calib->counter) {
		get_avr_value(calib, &calib->point[0]);
		sprintf(buf, "x: %d\ny: %d", calib->point[0].x, calib->point[0].y);
		lv_obj_t *label_coord = lv_label_create(lv_disp_get_scr_act(NULL), NULL);
		lv_label_set_text(label_coord, buf);
		printf_label(calib, "Click the circle in\nlower right-hand corner\n%u left", TOUCH_NUMBER);
		lv_obj_set_pos(calib->circle, calib->hres - CIRCLE_OFFSET - CIRCLE_SIZE,
			       calib->vres - CIRCLE_OFFSET - CIRCLE_SIZE);
		calib->counter = TOUCH_NUMBER - 1;

		return CALIB_STATE_BOTTOM_RIGHT;
	}

	printf_label(calib, "Click the circle in\nupper left-hand corner\n%u left", calib->counter);
	calib->counter--;

	return CALIB_STATE_UPPER_LEFT;
}

static enum calib_state handle_state_bottom_right(struct calib *calib)
{
	lv_indev_t *indev = lv_indev_get_act();
	char buf[64];

	lv_indev_get_point(indev, &calib->avr[calib->counter]);

	if (!calib->counter) {
		get_avr_value(calib, &calib->point[1]);
		sprintf(buf, "x: %d\ny: %d", calib->point[1].x, calib->point[1].y);
		lv_obj_t *label_coord = lv_label_create(lv_disp_get_scr_act(NULL), NULL);
		lv_label_set_text(label_coord, buf);
		lv_obj_set_pos(label_coord, calib->hres - lv_obj_get_width(label_coord), 
			       calib->vres - lv_obj_get_height(label_coord));
		printf_label(calib, "Click the screen\nto leave calibration");
		lv_obj_del(calib->circle);

		return CALIB_STATE_WAIT_LEAVE;
	}

	printf_label(calib, "Click the circle in\nlower right-hand corner\n%u left", calib->counter);
	calib->counter--;

	return CALIB_STATE_BOTTOM_RIGHT;
}

static void save_current_calibration()
{
	nvs_handle hdl;
	esp_err_t err;

	err = nvs_open(TAG, NVS_READWRITE, &hdl);
	if (err) {
		ESP_LOGE(TAG, "Unable to open %s nvm space %s\n", TAG,
			 esp_err_to_name(err));
		return ;
	}

	err = nvs_set_float(hdl, "ax", ax);
	if (err)
		goto cleanup;
	err = nvs_set_float(hdl, "bx", bx);
	if (err)
		goto cleanup;
	err = nvs_set_float(hdl, "ay", ay);
	if (err)
		goto cleanup;
	err = nvs_set_float(hdl, "by", by);
	if (err)
		goto cleanup;

cleanup:
	nvs_commit(hdl);
	nvs_close(hdl);
}

static void compute_calib(int y1, int x1, int y2, int x2, float *a, float *b)
{
	*a = (float)(y1 - y2) / (float)(x1 - x2);
	*b = (float)(y2 * x1 - y1 * x2) / (float)(x1 - x2);
}

static void handle_state_wait_leave(struct calib *calib)
{
	lv_indev_t *indev;
	/* compute calibration data */
	compute_calib(CIRCLE_OFFSET + CIRCLE_SIZE / 2 ,calib->point[0].x,
		      calib->hres - CIRCLE_OFFSET - CIRCLE_SIZE / 2, calib->point[1].x,
		      &ax, &bx);
	ESP_LOGD(TAG, "a = %f / b = %f\n", ax, bx);
	compute_calib(CIRCLE_OFFSET + CIRCLE_SIZE / 2 ,calib->point[0].y,
		      calib->vres - CIRCLE_OFFSET - CIRCLE_SIZE / 2, calib->point[1].y,
		      &ay, &by);
	ESP_LOGD(TAG, "a = %f / b = %f\n", ay, by);
	save_current_calibration();
	/* setup-calibration data */
	indev = lv_indev_get_act();
	read_cb_save = indev->driver.read_cb;
	indev->driver.read_cb = read_cb_calibrate;

	/* restore previous screen */
	lv_disp_load_scr(calib->prev_scr);
	/* delete calibration screen and associate datas*/
	lv_obj_del(calib->scr);
	free(calib);
}

static void btn_event_cb(lv_obj_t *scr, lv_event_t event)
{
	struct calib *calib = (struct calib *) lv_obj_get_user_data(lv_disp_get_scr_act(NULL));

	if( event != LV_EVENT_CLICKED)
		return;

	ESP_LOGD(TAG, "execute state %d\n", calib->state);
	switch (calib->state) {
	case CALIB_STATE_UPPER_LEFT:
		calib->state = handle_state_upper_left(calib);
		break;
	case CALIB_STATE_BOTTOM_RIGHT:
		calib->state = handle_state_bottom_right(calib);
		break;
	case CALIB_STATE_WAIT_LEAVE:
		handle_state_wait_leave(calib);
		break;
	default:
		ESP_LOGE(TAG, "state %d not supported\n", calib->state);
		assert(0);
	}
}

static int is_restore_calibration_succeed()
{
	nvs_handle hdl;
	esp_err_t err;

	err = nvs_open(TAG, NVS_READWRITE, &hdl);
	if (err) {
		ESP_LOGE(TAG, "Unable to open %s nvm space %s\n", TAG,
			 esp_err_to_name(err));
		return 1;
	}

	err = nvs_get_float(hdl, "ax", &ax);
	if (err)
		goto cleanup;
	err = nvs_get_float(hdl, "bx", &bx);
	if (err)
		goto cleanup;
	err = nvs_get_float(hdl, "ay", &ay);
	if (err)
		goto cleanup;
	err = nvs_get_float(hdl, "by", &by);
	if (err)
		goto cleanup;

	ESP_LOGD(TAG, "restore ax = %f\n", ax);
	ESP_LOGD(TAG, "restore bx = %f\n", bx);
	ESP_LOGD(TAG, "restore ay = %f\n", ay);
	ESP_LOGD(TAG, "restore by = %f\n", by);

cleanup:
	nvs_commit(hdl);
	nvs_close(hdl);

	return err != ESP_ERR_NVS_NOT_FOUND;
}

static void calibration_start_internal()
{
	struct calib *calib;

	ESP_LOGI(TAG, "start calibration\n");
	calib = malloc(sizeof(struct calib));
	assert(calib);
	memset(calib , 0, (sizeof(struct calib)));

	calib->prev_scr = lv_disp_get_scr_act(NULL);
	calib->scr = lv_obj_create(NULL, NULL);
	assert(calib->scr);
	lv_obj_set_user_data(calib->scr, calib);
	lv_disp_load_scr(calib->scr);
	calib->hres = lv_disp_get_hor_res(NULL);
	calib->vres = lv_disp_get_ver_res(NULL);
	lv_obj_set_size(calib->scr, calib->hres, calib->vres);

	calib->btn = lv_btn_create(calib->scr, NULL);
	assert(calib->btn);
	lv_obj_set_size(calib->btn, calib->hres, calib->vres);
	lv_btn_set_style(calib->btn, LV_BTN_STYLE_REL, &lv_style_transp);
	lv_btn_set_style(calib->btn, LV_BTN_STYLE_PR, &lv_style_transp);
	lv_obj_set_event_cb(calib->btn, btn_event_cb);
	lv_btn_set_layout(calib->btn, LV_LAYOUT_OFF);

	calib->label_msg = lv_label_create(lv_disp_get_scr_act(NULL), NULL);
	assert(calib->label_msg);
	lv_label_set_align(calib->label_msg, LV_LABEL_ALIGN_CENTER);
	printf_label(calib, "Click the circle in\nupper left-hand corner\n%u left", TOUCH_NUMBER);

	lv_style_copy(&calib->style_circ, &lv_style_pretty_color);
	calib->style_circ.body.radius = LV_RADIUS_CIRCLE;
	calib->circle = lv_obj_create(lv_disp_get_scr_act(NULL), NULL);
	lv_obj_set_size(calib->circle, CIRCLE_SIZE, CIRCLE_SIZE);
	assert(calib->circle);
	lv_obj_set_style(calib->circle, &calib->style_circ);
	lv_obj_set_click(calib->circle, false);
	lv_obj_set_pos(calib->circle, CIRCLE_OFFSET, CIRCLE_OFFSET);

	calib->counter = TOUCH_NUMBER - 1;
	calib->state = CALIB_STATE_UPPER_LEFT;
}

void calibration_setup(lv_indev_drv_t *indev_drv)
{
	if (is_restore_calibration_succeed()) {
		ESP_LOGI(TAG, "restore calibration\n");
		read_cb_save = indev_drv->read_cb;
		indev_drv->read_cb = read_cb_calibrate;

		return;
	}

	calibration_start_internal();
}

void calibration_start()
{
	/* remove calibration correction if in used */
	if (read_cb_save) {
		lv_indev_t *indev = lv_indev_get_act();
		indev->driver.read_cb = read_cb_save;
	}

	calibration_start_internal();
}
