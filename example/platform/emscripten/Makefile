# Makefile.local should define EMSCRIPTEN_PATH
-include Makefile.local

EMSDK = $(EMSCRIPTEN_PATH)/emsdk
$(shell $(EMSDK) construct_env)

# Modified by new_project.py
GLFM_ROOT := ../../glfm
APP_ROOT := ../..
MAL_ROOT := ../../..

OUT_NAME = index
BIN = bin
INCLUDES = -I $(GLFM_ROOT)/include -I $(MAL_ROOT)/include
SOURCES := $(GLFM_ROOT)/src/glfm_platform_emscripten.c \
           $(wildcard $(MAL_ROOT)/src/*.c) \
           $(wildcard $(APP_ROOT)/src/*.c)

build: init
	. emsdk_set_env.sh; emcc -Wall -O2 -std=c99 --memory-init-file 0 \
	--shell-file shell.html \
	--pre-js emscripten-pre.js \
	--preload-file $(APP_ROOT)/assets@/ $(INCLUDES) -o $(BIN)/$(OUT_NAME).html \
	$(SOURCES)

init:
	mkdir -p $(BIN)
	
clean:
	rm -f $(BIN)/$(OUT_NAME).html
	rm -f $(BIN)/$(OUT_NAME).data
	rm -f $(BIN)/$(OUT_NAME).js
