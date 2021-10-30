#ifndef __ID3__
#define __ID3__ 1

struct id3_meta {
	char *artist;
	char *album;
	char *title;
	int track_nb;
	int duration_in_ms;
};

int id3_get(char *filename, struct id3_meta *meta);
void id3_put(struct id3_meta *meta);
int id3_dup_meta(struct id3_meta *meta, struct id3_meta *dup_meta);

#endif