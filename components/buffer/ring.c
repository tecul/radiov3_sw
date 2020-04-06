#include "ring.h"

#include <assert.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

struct ring {
	RingbufHandle_t *hdl;
	int size_in_kb;
};

void *ring_create(int size_in_kb)
{
	struct ring *self = malloc(sizeof(struct ring));
	assert(self);

	self->hdl = xRingbufferCreate(size_in_kb * 1024, RINGBUF_TYPE_BYTEBUF);
	assert(self->hdl);
	self->size_in_kb = size_in_kb;

	return self;
}

void ring_destroy(void *hdl)
{
	struct ring *self = hdl;

	assert(hdl);
	vRingbufferDelete(self->hdl);

	free(hdl);
}

int ring_push(void *hdl, int size_in_byte, void *data)
{
	struct ring *self = hdl;
	int res;

	assert(hdl);
	res = xRingbufferSend(self->hdl, data, size_in_byte, 100 / portTICK_PERIOD_MS);

	return res ? 0 : -1;
}

int ring_pop(void *hdl, int *size_in_byte, void *data)
{
	struct ring *self = hdl;
	void *ref;

	assert(hdl);
	ref = xRingbufferReceiveUpTo(self->hdl, (size_t *) size_in_byte, 100 / portTICK_PERIOD_MS, *size_in_byte);
	if (!ref)
		return -1;
	memcpy(data, ref, *size_in_byte);
	vRingbufferReturnItem(self->hdl, ref);

	return 0;
}

void ring_reset(void *hdl)
{
	struct ring *self = hdl;
	int size_in_byte;
	void *ref;

	assert(hdl);
	while (1) {
		ref = xRingbufferReceiveUpTo(self->hdl, (size_t *) &size_in_byte, 0, (size_t )self->size_in_kb * 1024);
		if (!ref)
			return ;
		vRingbufferReturnItem(self->hdl, ref);
	}
}

int ring_level(void *hdl)
{
	struct ring *self = hdl;

	assert(hdl);
	return ((self->size_in_kb * 1024 - xRingbufferGetCurFreeSize(self->hdl)) * 100) /
		(self->size_in_kb * 1024);
}
