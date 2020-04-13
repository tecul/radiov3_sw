#ifndef __DB__
#define __DB__ 1

int create_db(char *filename, char *root_dir);

void *open_db(char *filename);
void close_db(void *db_hdl);
int get_artist_nb(void);
void *get_artist(int index);
char *get_artist_name(void *artist_hdl);
int get_album_nb(void *artist_hdl);
void *get_album(void *artist_hdl, int index);
char *get_album_name(void *album_hdl);
int get_song_nb(void *album_hdl);
void *get_song(void *album_hdl, int index);
char *get_song_name(void *song_hdl);
char *get_song_filename(void *song_hdl);
char *get_song_artist_name(void *song_hdl);
char *get_song_album_name(void *song_hdl);

#endif