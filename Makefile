.DEFAULT_GOAL := all

PYTHON ?= python3
PS2DEV ?= /opt/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT ?= $(PS2DEV)/gsKit
EE_TOOL_PREFIX ?= mips64r5900el-ps2-elf-
EE_CC ?= $(EE_TOOL_PREFIX)gcc
EE_LINKFILE ?= $(PS2SDK)/ee/startup/linkfile
RESOURCE_DIR := resources
OBJ_DIR := build/obj/ee
BIN_DIR := build/bin
OUTPUT_DIR := build/output
RESOURCE_FILES := $(shell find "$(RESOURCE_DIR)" -type f -print | sed 's/ /\\ /g')
RESOURCE_DIRS := $(shell find "$(RESOURCE_DIR)" -type d -print | sed 's/ /\\ /g')
DATA_PAK := $(OUTPUT_DIR)/usb/assets/DATA.PAK
BIN := $(BIN_DIR)/Granny.elf
OUTPUT_ELF := $(OUTPUT_DIR)/usb/Granny.elf
OBJS := $(OBJ_DIR)/main.o $(OBJ_DIR)/core/assets.o $(OBJ_DIR)/core/dbglog.o $(OBJ_DIR)/scene/model/model.o $(OBJ_DIR)/scene/model/model_loader.o $(OBJ_DIR)/scene/model/model_animation.o $(OBJ_DIR)/entities/player/player.o $(OBJ_DIR)/render/renderer.o $(OBJ_DIR)/render/tim2.o $(OBJ_DIR)/render/font_engine.o $(OBJ_DIR)/render/dev_hud.o

EE_INCS := -Iinclude -Isrc -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I$(PS2SDK)/ee/libcglue/include -I$(PS2SDK)/ee/erl/include -I$(PS2SDK)/ee/rpc/pad/include -I$(PS2SDK)/ee/rpc/filexio/include -isystem $(GSKIT)/include
EE_LDFLAGS := -L$(PS2SDK)/ee/lib -L$(PS2SDK)/ee/rpc/pad/lib -L$(PS2SDK)/ee/rpc/filexio/lib -L$(GSKIT)/lib
EE_OPTFLAGS ?= -O2
EE_CFLAGS := -D_EE -G0 $(EE_OPTFLAGS) -Wall -Wextra -std=c99 -MMD -MP
EE_LIBS := -lpad -lfileXio -lgskit_toolkit -lgskit -ldmakit -ldebug -lm

.PHONY: all build assets usb clean distclean

all: $(OUTPUT_ELF) $(DATA_PAK)

build: all

assets: $(DATA_PAK)

usb: $(OUTPUT_ELF) $(DATA_PAK)

$(DATA_PAK): $(RESOURCE_FILES) $(RESOURCE_DIRS) tools/pack_resources.py Makefile
	@mkdir -p $(@D)
	$(PYTHON) tools/pack_resources.py --resource-dir "$(RESOURCE_DIR)" --output "$@"

SRC_HEADERS := $(shell find src -type f -name '*.h' -print)
INC_HEADERS := $(shell find include -type f -name '*.h' -print)

$(OBJ_DIR)/%.o: src/%.c Makefile $(INC_HEADERS) $(SRC_HEADERS)
	@mkdir -p $(@D)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(BIN): $(OBJS)
	@mkdir -p $(@D)
	$(EE_CC) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $@ $(OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) $(EE_LIBS)

$(OUTPUT_ELF): $(BIN)
	@mkdir -p $(@D)
	cp $< $@

clean:
	rm -rf build

distclean: clean

-include $(OBJS:.o=.d)
