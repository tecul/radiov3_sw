idf_component_register(SRCS "fetch_bt.c" "fetch_file.c" "fetch_socket_radio.c"
		       INCLUDE_DIRS "include"
		       REQUIRES buffer audio bluetooth downloader utils)