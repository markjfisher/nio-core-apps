CC := wcc
LD := wcl

CFLAGS += -0 -bt=dos -os -ms -s -q
CFLAGS += -i=$(APP_INCLUDE_DIR)
CFLAGS += -i=$(CONFIG_NIO_INCLUDE_DIR)
CFLAGS += -i=$(PLATFORM_INCLUDE_DIR)
CFLAGS += -i=$(NIO_INCLUDE_DIR)
CFLAGS += -i=msdos/include
CFLAGS += -D__MSDOS__
CFLAGS += -DFNSVC_LIST_MAX_PAYLOAD=$(FNSVC_LIST_MAX_PAYLOAD)

LDFLAGS += -q -0 -bt=dos -ms

define compile_c
	$(CC) $(CFLAGS) -fo=$@ $<
endef

define link_program
	$(LD) $(LDFLAGS) $(EXTRA_PROGRAM_LDFLAGS) -fe=$@ $^
endef
