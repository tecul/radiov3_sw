#ifndef __WIFI__
#define __WIFI__ 1

void wifi_init(void);
int wifi_connect_start(void);
void wifi_scan_start(void (*wifi_scan_done_cb)(void *), void *arg);
int wifi_set_credentials(char *ssid, char *pwd);
int wifi_is_connected(void);

#endif