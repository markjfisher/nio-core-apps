TARGETS := msdos atari linux amiga
BOOT_IMAGE_TARGETS := msdos atari
DEFAULT_TARGET := $(if $(TARGET),$(TARGET),all-targets)

.PHONY: all all-targets clean disk disk-all boot-disk boot-disk-all install-boot-disk $(TARGETS)

all: $(DEFAULT_TARGET)

$(TARGETS):
	$(MAKE) -f makefiles/build.mk TARGET=$@

all-targets: $(TARGETS)

disk:
	$(MAKE) -f makefiles/build.mk TARGET=$(if $(TARGET),$(TARGET),msdos) disk

disk-all:
	@for target in $(TARGETS); do \
		$(MAKE) -f makefiles/build.mk TARGET=$$target disk; \
	done

boot-disk:
	$(MAKE) -f makefiles/build.mk TARGET=$(if $(TARGET),$(TARGET),msdos) boot-disk

boot-disk-all:
	@for target in $(BOOT_IMAGE_TARGETS); do \
		$(MAKE) -f makefiles/build.mk TARGET=$$target boot-disk; \
	done

install-boot-disk:
	$(MAKE) -f makefiles/build.mk TARGET=$(if $(TARGET),$(TARGET),msdos) install-boot-disk

clean:
	rm -rf build
