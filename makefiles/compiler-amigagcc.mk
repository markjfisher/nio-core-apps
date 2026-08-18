CC := m68k-amigaos-gcc

CFLAGS += -Wall -Wextra -O2 -std=c99
CFLAGS += -mcpu=68000 -msoft-float
# Select clib2 headers as well as its libraries. Compiling against newlib
# headers and linking with clib2 can leave newlib-only symbols unresolved.
CFLAGS += -mcrt=clib2
CFLAGS += -I$(APP_INCLUDE_DIR)
CFLAGS += -I$(CONFIG_NIO_INCLUDE_DIR)
CFLAGS += -I$(PLATFORM_INCLUDE_DIR)
CFLAGS += -I$(NIO_INCLUDE_DIR)
CFLAGS += -I../fujinet-nio-driver/build/amiga/include
CFLAGS += -DFNSVC_LIST_MAX_PAYLOAD=$(FNSVC_LIST_MAX_PAYLOAD)

# Keep Amiga applications self-contained instead of requiring the optional
# mathieeedoubbas.library at process startup.
LDFLAGS += -mcpu=68000 -msoft-float -mcrt=clib2
LDFLAGS += -L../fujinet-nio-driver/build/amiga/lib -lfujinet-amiga-disk

define compile_c
	$(CC) $(CFLAGS) -MMD -MF $(@:.o=.d) -c -o $@ $<
endef

define link_program
	$(CC) -o $@ $^ $(LDFLAGS) $(EXTRA_PROGRAM_LDFLAGS) -lamiga
endef
