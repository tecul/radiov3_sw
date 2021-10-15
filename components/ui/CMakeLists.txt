idf_component_register(SRCS "album_menu.c" "artist_menu.c" "bt_player.c" "main_menu.c" "music_player.c" "paging_menu.c" "playlist_menu.c" "radio_menu.c" "radio_player.c" "settings.c" "song_menu.c" "system_menu.c" "wifi_setting.c" "theme.c"
		       INCLUDE_DIRS "include"
		       REQUIRES lvgl ts_calibration db ota_update)