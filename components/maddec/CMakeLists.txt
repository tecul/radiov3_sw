idf_component_register(SRCS "bit.c" "decoder.c" "fixed.c" "frame.c" "huffman.c" "layer12.c" "layer3.c" "maddec.c" "stream.c" "synth.c" "timer.c" "version.c"
		       INCLUDE_DIRS "." "include"
		       REQUIRES buffer renderer)