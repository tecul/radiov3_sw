#include "fetch_bt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bluetooth.h"
#include "esp_log.h"

#include "stb350.h"

static const char* TAG = "rv3.fetch_bt";

static struct {
	void *renderer_hdl;
	int is2_nb;
} fetch_bt;

static void audio_cfg(int sample_rate)
{
	i2s_set_sample_rates(fetch_bt.is2_nb, sample_rate);
}

static void audio_data_cb(const uint8_t *data, uint32_t len)
{
	size_t bytes_written;

	stb350_write(fetch_bt.renderer_hdl, data, len, &bytes_written, portMAX_DELAY);
}

void *fetch_bt_create(void *renderer_hdl, int is2_nb)
{
	fetch_bt.renderer_hdl = renderer_hdl;
	fetch_bt.is2_nb = is2_nb;

	return &fetch_bt;
}

void fetch_bt_start(void *hdl, void *hdl_cb, audio_track_info_cb track_info_cb)
{
	int ret;

	assert(hdl == &fetch_bt);
	ret = bluetooth_enable(audio_cfg, audio_data_cb, hdl_cb, track_info_cb);
	if (ret)
		ESP_LOGE(TAG, "Unable to enable bluetooth => %d", ret);
}

void fetch_bt_stop(void *hdl)
{
	int ret;

	assert(hdl == &fetch_bt);
	ret = bluetooth_disable();
	if (ret)
		ESP_LOGE(TAG, "Unable to disable bluetooth => %d", ret);
}

void fetch_bt_next(void *hdl)
{
	int ret;

	assert(hdl == &fetch_bt);
	ret = bluetooth_cmd_next();
	if (ret)
		ESP_LOGE(TAG, "Unable to jump to next track => %d", ret);
}
