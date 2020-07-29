#ifndef __DB__
#define __DB__ 1

#include "id3.h"

int update_db(char *dirname, char *root_dir);

void *db_open(char *dirname);
void db_close(void *hdl);
int db_artist_get_nb(void *hdl);
char *db_artist_get(void *hdl, int index);
int db_album_get_nb(void *hdl, char *artist);
char *db_album_get(void *hdl, char *artist, int index);
int db_song_get_nb(void *hdl, char *artist, char *album);
char *db_song_get(void *hdl, char *artist, char *album, int index);
char *db_song_get_filepath(void *hdl, char *artist, char *album, char *song);
int db_song_get_meta(void *hdl, char *artist, char *album, char *song, struct id3_meta *meta);
int db_song_get_meta_from_file(void *hdl, char *path, struct id3_meta *meta);

void db_put_item(void *hdl, char *name);
void db_put_meta(void *hdl, struct id3_meta *meta);

#endif