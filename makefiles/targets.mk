PLATFORM_msdos := msdos
PLATFORM_atari := atari
PLATFORM_bbc := bbc
PLATFORM_bbc-clib := bbc
PLATFORM_linux := linux

COMPILER_FAMILY_msdos := wcc
COMPILER_FAMILY_atari := cc65
COMPILER_FAMILY_bbc := cc65
COMPILER_FAMILY_bbc-clib := cc65
COMPILER_FAMILY_linux := gcc

PROGRAM_EXT_msdos := .exe
PROGRAM_EXT_atari := .xex
PROGRAM_EXT_bbc :=
PROGRAM_EXT_bbc-clib :=
PROGRAM_EXT_linux :=

NIO_LIB_TARGET_msdos := msdos-ioctl
NIO_LIB_TARGET_atari := atari
NIO_LIB_TARGET_bbc := bbc
NIO_LIB_TARGET_bbc-clib := bbc-clib
NIO_LIB_TARGET_linux := linux

NIO_LIB_FILE_msdos := $(FUJINET_NIO_LIB)/build/fujinet-nio-msdos-ioctl.lib
NIO_LIB_FILE_atari := $(FUJINET_NIO_LIB)/build/fujinet-nio-atari.lib
NIO_LIB_FILE_bbc := $(FUJINET_NIO_LIB)/build/fujinet-nio-bbc.lib
NIO_LIB_FILE_bbc-clib := $(FUJINET_NIO_LIB)/build/fujinet-nio-bbc-clib.lib
NIO_LIB_FILE_linux := $(FUJINET_NIO_LIB)/build/fujinet-nio-linux.a

PLATFORM := $(PLATFORM_$(TARGET))
COMPILER_FAMILY := $(COMPILER_FAMILY_$(TARGET))
PROGRAM_EXT := $(PROGRAM_EXT_$(TARGET))
NIO_LIB_TARGET := $(NIO_LIB_TARGET_$(TARGET))
NIO_LIB_FILE := $(NIO_LIB_FILE_$(TARGET))

ifeq ($(PLATFORM),)
$(error Unknown TARGET '$(TARGET)'. Supported targets: msdos atari bbc bbc-clib linux)
endif
