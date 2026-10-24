SHELL := /usr/bin/env bash
.DEFAULT_GOAL := all

TARGET ?= msdos
FUJINET_NIO_LIB ?= ../fujinet-nio-lib

include makefiles/targets.mk

FNSVC_LIST_MAX_PAYLOAD ?= 420

APP_DIR := apps
SRC_DIR := src
APP_INCLUDE_DIR := include/common
CONFIG_NIO_INCLUDE_DIR := include/common
PLATFORM_INCLUDE_DIR := include/platform/$(PLATFORM)
NIO_INCLUDE_DIR := $(FUJINET_NIO_LIB)/include
BUILD_DIR ?= build
TARGET_BUILD_DIR ?= $(BUILD_DIR)/$(TARGET)

# The repository Makefile is the public Amiga entry point.  It supplies a
# named Workbench output directory; refusing this legacy default prevents an
# incompatible binary from silently reappearing in build/amiga/bin.
ifeq ($(TARGET),amiga)
ifeq ($(TARGET_BUILD_DIR),$(BUILD_DIR)/amiga)
$(error Amiga builds require TARGET_BUILD_DIR=build/amiga/wb13, wb31, or wb32; use `make amiga`)
endif
endif
OBJ_DIR := $(TARGET_BUILD_DIR)/obj
BIN_DIR := $(TARGET_BUILD_DIR)/bin
DISK_DIR := $(TARGET_BUILD_DIR)/disk

PLATFORM_APP_DIR := $(APP_DIR)/platform/$(PLATFORM)
PLATFORM_APP_SRCS := $(wildcard $(PLATFORM_APP_DIR)/*.c)
PLATFORM_APP_NAMES := $(basename $(notdir $(PLATFORM_APP_SRCS)))
GENERIC_APP_SRCS := $(filter-out $(PLATFORM_APP_NAMES:%=$(APP_DIR)/%.c),$(sort $(wildcard $(APP_DIR)/*.c)))
APP_SRCS := $(sort $(PLATFORM_APP_SRCS) $(GENERIC_APP_SRCS))
PROGRAMS_ALL := $(sort $(basename $(notdir $(APP_SRCS))))
PROGRAMS_EXCLUDE_msdos :=
PROGRAMS_EXCLUDE_atari := fboot
PROGRAMS_EXCLUDE_linux :=
PROGRAMS_EXCLUDE := $(PROGRAMS_EXCLUDE_$(TARGET))
ifeq ($(AMIGA_WB13),1)
# SystemTags()/SYS_Asynch used by fmountrestore is a Kickstart 2.0 API.
PROGRAMS_EXCLUDE += fmountrestore
endif
PROGRAMS := $(filter-out $(PROGRAMS_EXCLUDE),$(PROGRAMS_ALL))

COMMON_SRCS := $(SRC_DIR)/common/fnsvc.c $(SRC_DIR)/platform/$(PLATFORM)/fnctl.c
ifeq ($(TARGET),amiga)
COMMON_SRCS += $(SRC_DIR)/platform/amiga/amiga_stack.c
endif
COMMON_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(COMMON_SRCS))
APP_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(APP_SRCS))
PROGRAM_BINS := $(PROGRAMS:%=$(BIN_DIR)/%$(PROGRAM_EXT))
DEPENDS := $(COMMON_OBJS:.o=.d) $(APP_OBJS:.o=.d)

ifeq ($(COMPILER_FAMILY),wcc)
include makefiles/compiler-wcc.mk
else ifeq ($(COMPILER_FAMILY),cc65)
include makefiles/compiler-cc65.mk
else ifeq ($(COMPILER_FAMILY),gcc)
include makefiles/compiler-gcc.mk
else ifeq ($(COMPILER_FAMILY),amigagcc)
include makefiles/compiler-amigagcc.mk
else
$(error Unknown compiler family '$(COMPILER_FAMILY)' for TARGET=$(TARGET))
endif

DISK_TARGETS :=
-include makefiles/disk-$(TARGET).mk
-include makefiles/boot-disk.mk

.PHONY: all clean disk boot-disk install-boot-disk $(PROGRAMS) $(DISK_TARGETS) $(BOOT_DISK_TARGETS)
.SECONDARY: $(APP_OBJS) $(COMMON_OBJS)

all: $(PROGRAM_BINS)

disk: $(DISK_TARGETS)

$(PROGRAMS): %: $(BIN_DIR)/%$(PROGRAM_EXT)

-include $(DEPENDS)

$(NIO_LIB_FILE):
	$(MAKE) -C $(FUJINET_NIO_LIB) $(NIO_LIB_TARGET)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(call compile_c)

FNACCEPT_COMMON_OBJS := $(COMMON_OBJS)
FNRESIDENT_COMMON_OBJS := $(COMMON_OBJS)
ifeq ($(TARGET),atari)
# fnaccept exercises only the published fujinet-nio-lib API.  Do not link the
# interactive app-control/directory-service helpers into its constrained Atari
# acceptance image.  Other targets retain the normal common-object link.
FNACCEPT_COMMON_OBJS :=
# fnresident is likewise a self-contained public-library image.  Linking
# fnsvc/fnctl would reserve unrelated application state in the resident range.
FNRESIDENT_COMMON_OBJS :=
endif

define APP_PROGRAM_RULE
$(BIN_DIR)/$(1)$(PROGRAM_EXT): $(OBJ_DIR)/$$(patsubst %.c,%.o,$$(filter %/$(1).c,$$(APP_SRCS))) $$(if $$(filter fnaccept,$(1)),$$(FNACCEPT_COMMON_OBJS),$$(if $$(filter fnresident,$(1)),$$(FNRESIDENT_COMMON_OBJS),$$(COMMON_OBJS))) $$(NIO_LIB_FILE) | $(BIN_DIR)
	$$(call link_program)
endef

$(foreach prog,$(PROGRAMS),$(eval $(call APP_PROGRAM_RULE,$(prog))))

ifeq ($(TARGET),atari)
# fnresident is a deliberately resident CIO image.  Keep its handler table in
# the same linked image as fn_init() and all of the library state it reaches;
# the map/runtime gate then reserves that whole image through MEMLO.
FNRESIDENT_HANDLER_SRC := $(PLATFORM_APP_DIR)/fnresident_handler.s
FNRESIDENT_HANDLER_OBJ := $(OBJ_DIR)/$(FNRESIDENT_HANDLER_SRC:.s=.o)

$(FNRESIDENT_HANDLER_OBJ): $(FNRESIDENT_HANDLER_SRC) | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) -t $(TARGET) -c $(CFLAGS) -o $@ $<

$(BIN_DIR)/fnresident$(PROGRAM_EXT): $(FNRESIDENT_HANDLER_OBJ)
$(BIN_DIR)/fnresident$(PROGRAM_EXT): ATARI_START_ADDRESS = 0x1D00
$(BIN_DIR)/fnresident$(PROGRAM_EXT): ATARI_LINK_CONFIG = makefiles/cc65-atari-resident.cfg

# The normal fnresident XEX remains the fixed-image regression scaffold.  The
# production-loader increment builds this separate crt0-free o65 payload; it
# contains the real public Atari archive closure and is entered by a resident
# CIO vector after the transient loader has initialized it.
FNRESIDENT_MODULE_DIR := $(PLATFORM_APP_DIR)/resident
FNRESIDENT_MODULE_C := $(FNRESIDENT_MODULE_DIR)/fnresident_module.c
FNRESIDENT_MODULE_S := $(FNRESIDENT_MODULE_DIR)/fnresident_module_handler.s
FNRESIDENT_MODULE_C_OBJ := $(OBJ_DIR)/$(FNRESIDENT_MODULE_C:.c=.o)
FNRESIDENT_MODULE_S_OBJ := $(OBJ_DIR)/$(FNRESIDENT_MODULE_S:.s=.o)
FNRESIDENT_MODULE := $(BIN_DIR)/fnresident-module.o65

.PHONY: fnresident-module
fnresident-module: $(FNRESIDENT_MODULE)

$(FNRESIDENT_MODULE_C_OBJ): $(FNRESIDENT_MODULE_C) | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(call compile_c)

$(FNRESIDENT_MODULE_S_OBJ): $(FNRESIDENT_MODULE_S) | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) -t $(TARGET) -c $(CFLAGS) -o $@ $<

$(FNRESIDENT_MODULE): $(FNRESIDENT_MODULE_C_OBJ) $(FNRESIDENT_MODULE_S_OBJ) $(NIO_LIB_FILE) | $(BIN_DIR)
	ld65 -C ../cc65/cfg/module.cfg -o $@ -m $(basename $@).map -Ln $(basename $@).lbl \
		$(FNRESIDENT_MODULE_C_OBJ) $(FNRESIDENT_MODULE_S_OBJ) $(NIO_LIB_FILE) $(CC65_HOME)/lib/atari.lib
endif

# The public-API acceptance XEX pulls the bounded DiskDevice client in
# addition to Clock/File/Network.  Keep its larger code image below the Atari
# shared transport state without moving ordinary utilities or their links.
ifeq ($(TARGET),atari)
$(BIN_DIR)/fnaccept$(PROGRAM_EXT): ATARI_START_ADDRESS = 0x0D00
endif

$(OBJ_DIR):
	mkdir -p $@

$(BIN_DIR):
	mkdir -p $@

$(DISK_DIR):
	mkdir -p $@

clean:
	rm -rf $(TARGET_BUILD_DIR)
