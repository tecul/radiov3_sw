idf_component_register(SRCS "mongoose.c" "web_server.c"
		       INCLUDE_DIRS "include"
		       REQUIRES buffer system)

component_compile_definitions("MG_ENABLE_HTTP_STREAMING_MULTIPART=1" "MG_ENABLE_FILESYSTEM=1")