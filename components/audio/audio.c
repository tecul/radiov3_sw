#include "audio.h"

#include <stdio.h>
#include <assert.h>

#include "driver/i2c.h"

#include "stb350.h"
#include "fetch_socket_radio.h"
#include "fetch_file.h"
#include "ring.h"
#include "maddec.h"
#include "fetch_bt.h"
#include "esp_log.h"

static const char* TAG = "rv3.audio";

#define MIN(a,b)	((a)<(b)?(a):(b))
#define MAX(a,b)	((a)>(b)?(a):(b))

#define I2S_DMA_SAMPLE_BUFFER_SZ	576
#define MAX_VOLUME			0
#define MIN_VOLUME			16
#define STEP_VOLUME			7

static void *stb350_hdl;
static void *socket_hdl;
static void *file_hdl;
static void *buffer_hdl;
static void *decoder_hdl;
static void *bluetooth_hdl;

static int is_playing = 0;
static int is_music_playing = 0;
static int volume = 8;

static int init_i2c0()
{
	i2c_config_t conf;
	int res;

	printf("configure i2c\n");
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = 25;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_io_num = 33;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 400000;
	res = i2c_param_config(I2C_NUM_0, &conf);
	if (res)
		return res;
	return i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

static int init_i2s0()
{
	i2s_config_t i2s_config = {
		.mode = I2S_MODE_MASTER | I2S_MODE_TX,
		.sample_rate = 44100,
		.bits_per_sample = 16,
		.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
		.communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
		.dma_buf_count = 8,
		.dma_buf_len = I2S_DMA_SAMPLE_BUFFER_SZ,
		.use_apll = true,
		.tx_desc_auto_clear = true,
		.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
	};
	i2s_pin_config_t pin_config = {
		.bck_io_num = GPIO_NUM_22,
		.ws_io_num = GPIO_NUM_21,
		.data_out_num = GPIO_NUM_27,
		.data_in_num = (-1)//Not used
	};

	i2s_driver_install(0, &i2s_config, 0, NULL);
	i2s_set_pin(0, &pin_config);

	i2s_stop(0);
	i2s_zero_dma_buffer(0);

	// Enable MCLK output
	PIN_FUNC_SELECT (PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
	WRITE_PERI_REG(PIN_CTRL, 0x3f0);

	return 0;
}

void audio_init()
{
	assert(init_i2c0() == 0);
	assert(init_i2s0() == 0);

	buffer_hdl = ring_create(144);
	assert(buffer_hdl);
	stb350_hdl = stb350_create(I2C_NUM_0, I2S_NUM_0, 26);
	assert(stb350_hdl);
	socket_hdl = fetch_socket_radio_create(buffer_hdl);
	assert(socket_hdl);
	file_hdl = fetch_file_create(buffer_hdl);
	assert(file_hdl);
	bluetooth_hdl = fetch_bt_create(stb350_hdl, 0);
	assert(bluetooth_hdl);
	decoder_hdl = maddec_create(buffer_hdl, stb350_hdl);
	assert(decoder_hdl);
	assert(stb350_init(stb350_hdl) == 0);
	assert(stb350_set_volume(stb350_hdl, volume * STEP_VOLUME) == 0);
	printf("audio init done\n");
}

void audio_radio_play(char *url, char *port_nb, char *path, int rate)
{
	if (is_playing)
		audio_radio_stop();

	i2s_set_sample_rates(0, rate);
	assert(stb350_start(stb350_hdl) == 0);
	fetch_socket_radio_start(socket_hdl, url, port_nb, path);
	maddec_start(decoder_hdl);

	is_playing = 1;
}

void audio_radio_stop()
{
	if (!is_playing)
		return ;

	maddec_stop(decoder_hdl);
	fetch_socket_radio_stop(socket_hdl);
	stb350_stop(stb350_hdl);
	ring_reset(buffer_hdl);

	is_playing = 0;
}

void audio_music_play(char *filepath)
{
	if (is_music_playing)
		audio_music_stop();

	assert(stb350_start(stb350_hdl) == 0);
	fetch_file_start(file_hdl, filepath);
	maddec_start(decoder_hdl);

	is_music_playing = 1;
}

void audio_music_stop()
{
	if (!is_music_playing)
		return ;

	maddec_stop(decoder_hdl);
	fetch_file_stop(file_hdl);
	stb350_stop(stb350_hdl);
	ring_reset(buffer_hdl);

	is_music_playing = 0;
}

void audio_bluetooth_play(void *hdl, audio_track_info_cb track_info_cb)
{
	if (!bluetooth_hdl)
		return ;

	fetch_bt_start(bluetooth_hdl, hdl, track_info_cb);
	assert(stb350_start(stb350_hdl) == 0);
}

void audio_bluetooth_next()
{
	ESP_LOGI(TAG,"audio_bluetooth_next %p", bluetooth_hdl);
	if (!bluetooth_hdl)
		return ;

	fetch_bt_next(bluetooth_hdl);
}

void audio_bluetooth_stop()
{
	if (!bluetooth_hdl)
		return ;

	stb350_stop(stb350_hdl);
	fetch_bt_stop(bluetooth_hdl);
}

int audio_sound_level_up()
{
	volume--;
	volume = MAX(MAX_VOLUME, volume);

	assert(stb350_set_volume(stb350_hdl, volume * STEP_VOLUME) == 0);

	return audio_sound_get_level();
}

int audio_sound_level_down()
{
	volume++;
	volume = MIN(MIN_VOLUME, volume);

	assert(stb350_set_volume(stb350_hdl, volume * STEP_VOLUME) == 0);

	return audio_sound_get_level();
}

int audio_sound_get_level()
{
	return MIN_VOLUME - volume;
}

int audio_sound_get_max_level()
{
	return MIN_VOLUME;
}

int audio_buffer_level()
{
	return ring_level(buffer_hdl);
}
