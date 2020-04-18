#include "id3.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct id3v2_header {
	char magic[3];
	uint8_t version_major;
	uint8_t version_minor;
	uint8_t flags;
	uint8_t size[4];
};

struct id3v2_frame_header {
	char frame_id[4];
	uint8_t size[4];
	uint8_t flags[2];
};

struct id3_parser {
	int fd;
	int hdr_size;
	int hdr_is_unsynchronisation;
	struct id3v2_header hdr;
	struct id3v2_frame_header fhdr;
	char *title;
	char *artist;
	int track_nb;
	char *album;
	int duration_in_ms;
};

static int utf16_to_utf8_len(uint16_t u16)
{
	if (u16 < 0x0080)
		return 1;
	else if (u16 < 0x0800)
		return 2;

	return 3;
}

static int iso_8859_1_to_utf8_len(uint8_t u8)
{
	if (u8 < 0x80)
		return 1;

	return 2;
}

static unsigned char *utf16_to_utf8_encode(uint16_t u16, unsigned char *buf)
{
	if (u16 < 0x0080) {
		buf[0] = u16 & 0xff;
		return buf + 1;
	} else if (u16 < 0x0800) {
		buf[0] = 0xc0 + (u16 >> 6);
		buf[1] = 0x80 + (u16 & 0x3f);
		return buf + 2;
	} else {
		buf[0] = 0xd0 + (u16 >> 12);
		buf[1] = 0x80 + ((u16 >> 6) & 0x3f);
		buf[2] = 0x80 + (u16 & 0x3f);
		return buf + 3;
	}
}

static unsigned char *iso_8859_1_to_utf8_one(uint8_t u8, unsigned char *buf)
{
	if (u8 < 0x80) {
		buf[0] = u8;
		return buf + 1;
	}
	buf[0] = 0xc0 + (u8 >> 6);
	buf[1] = 0x80 + (u8 & 0x3f);

	return buf + 2;
}

static char *iso_8859_1_to_utf8_with_len(char *buf, int maxlen)
{
	unsigned char *b;
	int len = 0;
	uint8_t u8;
	char *res;
	char *tmp;
	int ll;

	assert(maxlen > 0);
	ll = maxlen;
	b = (unsigned char *) buf;
	do {
		u8 = (uint8_t ) b[0];
		len += iso_8859_1_to_utf8_len(u8);
		//printf("0x%02x : %d\n", u8, ll);
		b += 1;
		ll--;
	} while (u8 && ll);
	len += u8 ? 1 : 0;

	res = malloc(len);
	assert(res);

	ll = maxlen;
	b = (unsigned char *) buf;
	tmp = res;
	do {
		u8 = (uint8_t ) b[0];
		tmp = (char *) iso_8859_1_to_utf8_one(u8, (unsigned char *) tmp);
		b += 1;
		ll--;
	} while (u8 && ll);
	if (u8)
		*tmp = '\0';
	//printf("iso_8859_1_to_utf8 => %s\n", res);

	return res;
}

static char *utf16le_to_utf8_with_len(char *buf, int maxlen)
{
	int len = 0;
	uint16_t u16;
	unsigned char *b;
	char *res;
	char *tmp;
	int ll;

	assert(maxlen > 0);
	assert(maxlen % 2 == 0);

	ll = maxlen;
	b = (unsigned char *) buf;
	do {
		//printf("b[0] = 0x%02x b[1] = 0x%02x\n", b[0], b[1]);
		u16 = (b[1] << 8) + b[0];
		len += utf16_to_utf8_len(u16);
		b += 2;
		//printf("u16 = 0x%04x (%d)\n", u16, len);
		ll -= 2;
	} while (u16 && ll);
	len += u16 ? 1 : 0;
	//printf("will use %d bytes\n", len);

	res = malloc(len);
	assert(res);

	ll = maxlen;
	b = (unsigned char *) buf;
	tmp = res;
	do {
		u16 = (b[1] << 8) + b[0];
		tmp = (char *) utf16_to_utf8_encode(u16, (unsigned char *) tmp);
		b += 2;
		ll -= 2;
	} while (u16 && ll);
	if (u16)
		*tmp = '\0';
	//printf("utf16le_to_utf8 => %s\n", res);

	return res;
}

static char *utf16_to_utf8_with_len(char *buf, int len)
{
	if (buf[0] == 0xfe)
		assert(0);
	else
		return utf16le_to_utf8_with_len(&buf[2], len - 2);
}

static char *utf8_to_utf8_with_len(char *buf, int len)
{
	char *res;

	res = malloc(len);
	memcpy(res, buf, len);

	assert(res[len - 1] == 0);

	return res;
}

static char *convert_to_utf8_with_len(char *buf, int len)
{
	switch (buf[0]) {
	case 0:
		return iso_8859_1_to_utf8_with_len(&buf[1], len - 1);
	case 1:
		return utf16_to_utf8_with_len(&buf[1], len -1);
	case 2:
		assert(0);
	case 3:
		return utf8_to_utf8_with_len(&buf[1], len - 1);
	default:
		assert(0);
	}
}

static int id3_parse_header(struct id3_parser *parser)
{
	struct id3v2_header *hdr = &parser->hdr;
	int ret;
	int i;

	ret = read(parser->fd, hdr, sizeof(*hdr));
	if (ret != sizeof(*hdr))
		return -1;

	if (hdr->magic[0] != 'I' || hdr->magic[1] != 'D' || hdr->magic[2] != '3')
		return -2;

	if (hdr->flags & 0x80)
		parser->hdr_is_unsynchronisation = 1;
	if ((hdr->flags & 0x7f) != 0) {
		printf("hdr->flags != 0 : 0x%02x / v%d.%d \n", hdr->flags,
			hdr->version_major, hdr->version_minor);
		return -3;
	}

	parser->hdr_size = 0;
	for (i = 0; i < 4; ++i)
		parser->hdr_size = (parser->hdr_size << 7) + hdr->size[i];
	//printf("id3 tag size is %d bytes v%d.%d\n", parser->hdr_size,
	//       hdr->version_major, hdr->version_minor);

	return 0;
}

static int is_frame_id(struct id3v2_frame_header *fhdr, char *frame_id)
{
	return !strncmp(fhdr->frame_id, frame_id, 4);
}

static int is_tit2(struct id3v2_frame_header *fhdr)
{
	return is_frame_id(fhdr, "TIT2");
}

static int is_tpe1(struct id3v2_frame_header *fhdr)
{
	return is_frame_id(fhdr, "TPE1");
}

static int is_trck(struct id3v2_frame_header *fhdr)
{
	return is_frame_id(fhdr, "TRCK");
}

static int is_talb(struct id3v2_frame_header *fhdr)
{
	return is_frame_id(fhdr, "TALB");
}

static int is_tlen(struct id3v2_frame_header *fhdr)
{
	return is_frame_id(fhdr, "TLEN");
}

static void id3_3_0_parse_tit2(struct id3_parser *parser, char *buf, int len)
{
	parser->title = convert_to_utf8_with_len(buf, len);
}

static void id3_4_0_parse_tit2(struct id3_parser *parser, char *buf, int len)
{
	parser->title = convert_to_utf8_with_len(buf, len);
}

static void id3_3_0_parse_tpe1(struct id3_parser *parser, char *buf, int len)
{
	parser->artist = convert_to_utf8_with_len(buf, len);
}

static void id3_4_0_parse_tpe1(struct id3_parser *parser, char *buf, int len)
{
	parser->artist = convert_to_utf8_with_len(buf, len);
}

static void id3_3_0_parse_trck(struct id3_parser *parser, char *buf, int len)
{
	char *tmp = convert_to_utf8_with_len(buf, len);

	parser->track_nb = atoi(tmp);
	free(tmp);
}

static void id3_4_0_parse_trck(struct id3_parser *parser, char *buf, int len)
{
	char *tmp = convert_to_utf8_with_len(buf, len);

	parser->track_nb = atoi(tmp);
	free(tmp);
}

static void id3_3_0_parse_talb(struct id3_parser *parser, char *buf, int len)
{
	parser->album = convert_to_utf8_with_len(buf, len);
}

static void id3_4_0_parse_talb(struct id3_parser *parser, char *buf, int len)
{
	parser->album = convert_to_utf8_with_len(buf, len);
}

static void id3_3_0_parse_tlen(struct id3_parser *parser, char *buf, int len)
{
	char *tmp = convert_to_utf8_with_len(buf, len);

	parser->duration_in_ms = atoi(tmp);
	free(tmp);
}

static void id3_4_0_parse_tlen(struct id3_parser *parser, char *buf, int len)
{
	char *tmp = convert_to_utf8_with_len(buf, len);

	parser->duration_in_ms = atoi(tmp);
	free(tmp);
}

static void *unsynchronize(unsigned char *buf, int *size)
{
	char *res = malloc(*size);
	int res_size = 0;
	int i;

	assert(res);
	assert(*size);
	res[0] = buf[0];
	res_size = 1;
	for (i = 1; i < *size; i++) {
		if (buf[i - 1] == 0xff && buf[i] == 0x00)
			continue;
		res[res_size++] = buf[i];
	}
	free(buf);
	*size = res_size;

	return res;
}

static int id3_3_0_parse_frames(struct id3_parser *parser)
{
	struct id3v2_frame_header *fhdr = &parser->fhdr;
	int len = parser->hdr_size;
	unsigned char *buf;
	int size;
	int ret;
	int i;

	assert(parser->hdr_is_unsynchronisation == 0);
	while (len >= sizeof(struct id3v2_frame_header)) {
		//printf("> len = %d\n", len);
		size = 0;
		ret = read(parser->fd, fhdr, sizeof(*fhdr));
		if (ret != sizeof(*fhdr))
			return -1;
		len -= sizeof(*fhdr);

		/*if (fhdr->flags[0] != 0 || fhdr->flags[1]) {
			printf("fhdr->flags[] != 0 : 0x%02x 0x%02x\n", fhdr->flags[0],
				fhdr->flags[1]);
			printf("frame id %c%c%c%c\n", fhdr->frame_id[0], fhdr->frame_id[1],
				fhdr->frame_id[2], fhdr->frame_id[3]);
			return -3;
		}*/

		for (i = 0; i < 4; ++i)
			size = (size << 8) + fhdr->size[i];
		if (size > len)
			break;
		if (size == 0)
			break;

		/*printf("frame id %c%c%c%c\n", fhdr->frame_id[0], fhdr->frame_id[1],
			fhdr->frame_id[2], fhdr->frame_id[3]);

		printf("Will alloc %d bytes\n", size);*/
		buf = malloc(size);
		assert(buf);
		ret = read(parser->fd, buf, size);
		if (ret != size) {
			free(buf);
			return -4;
		}

		/*{
			for (i = 0; i < size; ++i)
				printf("%02x ", buf[i]);
			printf("\n");
		}*/

		if (is_tit2(fhdr))
			id3_3_0_parse_tit2(parser, (char *) buf, size);
		if (is_tpe1(fhdr))
			id3_3_0_parse_tpe1(parser, (char *) buf, size);
		if (is_trck(fhdr))
			id3_3_0_parse_trck(parser, (char *) buf, size);
		if (is_talb(fhdr))
			id3_3_0_parse_talb(parser, (char *) buf, size);
		if (is_tlen(fhdr))
			id3_3_0_parse_tlen(parser, (char *) buf, size);

		free(buf);
		len -= size;
	}

	return 0;
}

static int id3_4_0_parse_frames(struct id3_parser *parser)
{
	struct id3v2_frame_header *fhdr = &parser->fhdr;
	int len = parser->hdr_size;
	unsigned char *buf;
	int frame_size;
	int size;
	int ret;
	int i;
	int is_unsynchronisation;
	int has_data_length_indicator;

	while (len >= sizeof(struct id3v2_frame_header)) {
		is_unsynchronisation = parser->hdr_is_unsynchronisation;
		has_data_length_indicator = 0;
		size = 0;
		ret = read(parser->fd, fhdr, sizeof(*fhdr));
		if (ret != sizeof(*fhdr))
			return -1;
		len -= sizeof(*fhdr);

		if (fhdr->flags[0]) {
			printf("fhdr->flags[] != 0 : 0x%02x 0x%02x\n", fhdr->flags[0],
				fhdr->flags[1]);
			return -3;
		}

		if (fhdr->flags[1] & 2)
			is_unsynchronisation = 1;
		if (fhdr->flags[1] & 1)
			has_data_length_indicator = 1;
		if (fhdr->flags[1] & 0xfc) {
			printf("fhdr->flags[] != 0 : 0x%02x 0x%02x\n", fhdr->flags[0],
				fhdr->flags[1]);
			return -3;
		}

		for (i = 0; i < 4; ++i)
			size = (size << 7) + fhdr->size[i];
		if (size > len)
			break;
		if (size == 0)
			break;
		frame_size = size;

		/*printf("frame id %c%c%c%c / %d.%d\n", fhdr->frame_id[0], fhdr->frame_id[1],
			fhdr->frame_id[2], fhdr->frame_id[3], is_unsynchronisation,
			has_data_length_indicator);

		printf("Will alloc %d bytes\n", size);*/
		buf = malloc(size);
		assert(buf);
		ret = read(parser->fd, buf, size);
		if (ret != size) {
			free(buf);
			return -4;
		}

		/*{
			for (i = 0; i < size; ++i)
				printf("%02x ", buf[i]);
			printf("\n");
		}*/

		if (is_unsynchronisation)
			buf = unsynchronize(buf, &size);
		if (has_data_length_indicator) {
			buf += 4;
			size -= 4;
		}

		if (is_tit2(fhdr))
			id3_4_0_parse_tit2(parser, (char *) buf, size);
		if (is_tpe1(fhdr))
			id3_4_0_parse_tpe1(parser, (char *) buf, size);
		if (is_trck(fhdr))
			id3_4_0_parse_trck(parser, (char *) buf, size);
		if (is_talb(fhdr))
			id3_4_0_parse_talb(parser, (char *) buf, size);
		if (is_tlen(fhdr))
			id3_4_0_parse_tlen(parser, (char *) buf, size);

		if (has_data_length_indicator)
			buf -= 4;

		free(buf);
		len -= frame_size;
	}

	return 0;
}

static int id3_parse_frames(struct id3_parser *parser)
{
	struct id3v2_header *hdr = &parser->hdr;
	int version = hdr->version_major * 256 + hdr->version_minor;

	switch (version) {
	case 0x400:
		return id3_4_0_parse_frames(parser);
	case 0x300:
		return id3_3_0_parse_frames(parser);
	default:
		printf("unsupported id3 version %d.%d\n", hdr->version_major,
		       hdr->version_minor);
	}

	return -1;
}

static struct id3_parser *id3_create(const char *filename)
{
	struct id3_parser *parser;
	int ret;

	parser = malloc(sizeof(struct id3_parser));
	if (!parser)
		return NULL;
	memset(parser, 0, sizeof(struct id3_parser));

	parser->fd = open(filename, O_RDONLY);
	if (parser->fd < 0) {
		printf("Unable to open %s\n", filename);
		goto open_error;
	}

	ret = id3_parse_header(parser);
	if (ret < 0)
		goto parse_error;

	ret = id3_parse_frames(parser);
	if (ret)
		goto parse_error;

	close(parser->fd);

	return parser;

parse_error:
	close(parser->fd);
open_error:
	free(parser);

	return NULL;
}

int id3_get(char *filename, struct id3_meta *meta)
{
	struct id3_parser *parser = id3_create(filename);

	if (!parser)
		return -1;

	meta->artist = parser->artist;
	meta->album = parser->album;
	meta->title = parser->title;
	meta->track_nb = parser->track_nb;
	meta->duration_in_ms = parser->duration_in_ms;

	free(parser);

	return 0;
}

void id3_put(struct id3_meta *meta)
{
	if (meta->artist)
		free(meta->artist);
	if (meta->album)
		free(meta->album);
	if (meta->title)
		free(meta->title);
}
