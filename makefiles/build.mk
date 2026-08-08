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
TARGET_BUILD_DIR := $(BUILD_DIR)/$(TARGET)
OBJ_DIR := $(TARGET_BUILD_DIR)/obj
BIN_DIR := $(TARGET_BUILD_DIR)/bin
DISK_DIR := $(TARGET_BUILD_DIR)/disk

APP_SRCS := $(sort $(wildcard $(APP_DIR)/*.c))
PROGRAMS_ALL := $(basename $(notdir $(APP_SRCS)))
PROGRAMS_EXCLUDE_msdos :=
PROGRAMS_EXCLUDE_atari := fboot
PROGRAMS_EXCLUDE_linux :=
PROGRAMS_EXCLUDE := $(PROGRAMS_EXCLUDE_$(TARGET))
PROGRAMS := $(filter-out $(PROGRAMS_EXCLUDE),$(PROGRAMS_ALL))

COMMON_SRCS := $(SRC_DIR)/common/fnsvc.c $(SRC_DIR)/platform/$(PLATFORM)/fnctl.c
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

define APP_PROGRAM_RULE
$(BIN_DIR)/$(1)$(PROGRAM_EXT): $(OBJ_DIR)/$$(patsubst %.c,%.o,$$(filter %/$(1).c,$$(APP_SRCS))) $$(COMMON_OBJS) $$(NIO_LIB_FILE) | $(BIN_DIR)
	$$(call link_program)
endef

$(foreach prog,$(PROGRAMS),$(eval $(call APP_PROGRAM_RULE,$(prog))))

$(OBJ_DIR):
	mkdir -p $@

$(BIN_DIR):
	mkdir -p $@

$(DISK_DIR):
	mkdir -p $@

clean:
	rm -rf $(TARGET_BUILD_DIR)
