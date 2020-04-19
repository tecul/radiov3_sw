#ifndef __FETCH_FILE__
#define __FETCH_FILE__ 1

void *fetch_file_create(void *buffer_hdl);
void fetch_file_destroy(void *hdl);
void fetch_file_start(void *hdl, char *filename);
void fetch_file_stop(void *hdl);

#endif