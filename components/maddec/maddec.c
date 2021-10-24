#include "maddec.h"

#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "mad.h"

#include "ring.h"
#include "stb350.h"

static const char* TAG = "rv3.maddec";

#define INPUT_BUFFER_SIZE					2 * 1024

struct maddec {
	void *buffer_hdl;
	void *renderer_hdl;
	TaskHandle_t task;
	volatile bool is_active;
	SemaphoreHandle_t sem_en_of_task;
	struct mad_decoder decoder;
	unsigned char input_buffer[INPUT_BUFFER_SIZE];
	unsigned char *end_buffer;
	int16_t samples[1152][2];
	int has_set_rate;
};

static int refill(struct maddec *self, int remain)
{
	int free_space = INPUT_BUFFER_SIZE - remain;
	int res;

	while (1) {
		int size_in_out = free_space;
		res = ring_pop(self->buffer_hdl, &size_in_out, self->input_buffer + remain);
		if (res) {
			if (!self->is_active)
				return -1;
		} else {
			return size_in_out;
		}
	}
	assert(0);
}

static void maddec_task(void *arg)
{
	struct maddec *self = arg;
	int res;

	ESP_LOGI(TAG, "mad_decoder_run\n");
	res = mad_decoder_run(&self->decoder, MAD_DECODER_MODE_SYNC);
	ESP_LOGI(TAG, "mad_decoder_finish %d\n", res);

	xSemaphoreGive(self->sem_en_of_task);
	vTaskDelete(NULL);
}

static enum mad_flow input_func(void *data, struct mad_stream *stream)
{
	struct maddec *self = data;
	int remain = stream->next_frame ? self->end_buffer - stream->next_frame : 0;
	int ret;

	if (stream->next_frame)
		memmove(self->input_buffer, stream->next_frame, remain);

	ret = refill(self, remain);
	if (ret <= 0 || !self->is_active)
		return MAD_FLOW_STOP;

	mad_stream_buffer(stream, self->input_buffer, remain + ret);
	//printf("%s => %d\n", __FUNCTION__, ret);
	self->end_buffer = self->input_buffer + remain + ret;

	return MAD_FLOW_CONTINUE;
}

static enum mad_flow header_func(void *data, struct mad_header const *header)
{
	struct maddec *self = data;

	//printf("%s %ld.%ld\n", __FUNCTION__, header->duration.seconds, header->duration.fraction);
	if (self->has_set_rate)
		return MAD_FLOW_CONTINUE;

	i2s_set_sample_rates(0, header->samplerate);
	self->has_set_rate = 1;

	return MAD_FLOW_CONTINUE;
}

static enum mad_flow filter_func(void *data, struct mad_stream const *stream, struct mad_frame *frame)
{
	//printf("%s\n", __FUNCTION__);

	return MAD_FLOW_CONTINUE;
}

static inline int32_t scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static enum mad_flow output_func(void *data, struct mad_header const *header, struct mad_pcm *pcm)
{
	struct maddec *self = data;
	size_t i2s_bytes_write;
	int res;
	int i;

	for(i = 0; i < 1152; i++) {
		self->samples[i][0] = scale(pcm->samples[0][i]);
		self->samples[i][1] = scale(pcm->samples[1][i]);
	}

	while (self->is_active) {
		res = stb350_write(self->renderer_hdl, self->samples, sizeof(self->samples), &i2s_bytes_write, 100 / portTICK_PERIOD_MS);
		if (res)
			continue;
		break;
	}

	return MAD_FLOW_CONTINUE;
}

static enum mad_flow error_func(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
	//printf("%s decoding error 0x%04x (%s)\n", __FUNCTION__, stream->error, mad_stream_errorstr(stream));

	return MAD_FLOW_CONTINUE;
}

static enum mad_flow message_func(void *data, void *message, unsigned int *size)
{
	//printf("%s\n", __FUNCTION__);

	return MAD_FLOW_CONTINUE;
}

/* public api */
void *maddec_create(void *buffer_hdl, void *renderer_hdl)
{
	struct maddec *self = malloc(sizeof(struct maddec));
	assert(self);

	self->buffer_hdl = buffer_hdl;
	self->renderer_hdl = renderer_hdl;
	self->sem_en_of_task = xSemaphoreCreateBinary();
	assert(self->sem_en_of_task);

	mad_decoder_init(&self->decoder, self, input_func,
		header_func, filter_func, output_func,
		error_func, message_func);

	return self;
}

void maddec_destroy(void *hdl)
{
	struct maddec *self = hdl;

	assert(hdl);
	mad_decoder_finish(&self->decoder);
	vSemaphoreDelete(self->sem_en_of_task);

	free(hdl);
}

void maddec_start(void *hdl)
{
	struct maddec *self = hdl;
	BaseType_t res;

	assert(hdl);

	self->is_active = true;
	self->has_set_rate = false;
	res = xTaskCreatePinnedToCore(maddec_task, "maddec", 3 * 4096, hdl,
			tskIDLE_PRIORITY + 1, &self->task, tskNO_AFFINITY);
	assert(res == pdPASS);
}

void maddec_stop(void *hdl)
{
	struct maddec *self = hdl;

	assert(hdl);

	self->is_active = false;
	xSemaphoreTake(self->sem_en_of_task, portMAX_DELAY);
}
