CC := cl65

ATARI_START_ADDRESS ?= 0x2400
ATARI_LINK_CONFIG ?= makefiles/cc65-atari-nio.cfg
BBC_START_ADDRESS ?= 0x1900

CFLAGS += -Osir -O
CFLAGS += --include-dir $(APP_INCLUDE_DIR)
CFLAGS += --include-dir $(CONFIG_NIO_INCLUDE_DIR)
CFLAGS += --include-dir $(PLATFORM_INCLUDE_DIR)
CFLAGS += --include-dir $(NIO_INCLUDE_DIR)
CFLAGS += -DFNSVC_LIST_MAX_PAYLOAD=$(FNSVC_LIST_MAX_PAYLOAD)
ASMFLAGS += -D FNSVC_LIST_MAX_PAYLOAD=$(FNSVC_LIST_MAX_PAYLOAD)

LDFLAGS += -t $(TARGET)
ifeq ($(TARGET),atari)
LDFLAGS += --config $(ATARI_LINK_CONFIG) --start-addr $(ATARI_START_ADDRESS)
else ifneq ($(filter $(TARGET),bbc bbc-clib),)
LDFLAGS += --start-addr $(BBC_START_ADDRESS)
EXTRA_LINK_OBJS += $(FUJINET_NIO_LIB)/obj/$(TARGET)/platform/bbc/initenv.o
endif

define compile_c
	$(CC) -t $(TARGET) -c --create-dep $(@:.o=.d) --listing $(@:.o=.lst) $(CFLAGS) -o $@ $<
endef

define link_program
	$(CC) $(LDFLAGS) $(EXTRA_PROGRAM_LDFLAGS) --mapfile $(basename $@).map -Ln $(basename $@).lbl -o $@ $^ $(EXTRA_LINK_OBJS)
endef
