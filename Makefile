TARGETS := msdos atari linux amiga
BOOT_IMAGE_TARGETS := msdos atari
DEFAULT_TARGET := $(if $(TARGET),$(TARGET),all-targets)
AMIGA_PROFILES := wb13 wb31 wb32
AMIGA_PROFILE ?=

AMIGA_CRT_wb13 := nix13
AMIGA_CRT_wb31 := clib2
AMIGA_CRT_wb32 := clib2

.PHONY: all all-targets clean disk disk-all boot-disk boot-disk-all install-boot-disk $(TARGETS)

all: $(DEFAULT_TARGET)

msdos atari linux:
	$(MAKE) -f makefiles/build.mk TARGET=$@

# Amiga executables are always profile-qualified.  Supplying AMIGA_PROFILE
# builds one profile; plain `make amiga` produces all supported variants.
amiga:
ifeq ($(AMIGA_PROFILE),)
	@for profile in $(AMIGA_PROFILES); do \
		$(MAKE) -f makefiles/build.mk TARGET=amiga \
			TARGET_BUILD_DIR=build/amiga/$$profile \
			AMIGA_CRT=$$(if [ "$$profile" = wb13 ]; then echo nix13; else echo clib2; fi) \
			AMIGA_WB13=$$(if [ "$$profile" = wb13 ]; then echo 1; else echo 0; fi) \
			FUJINET_NIO_DRIVER_BUILD=../fujinet-nio-driver/build/amiga/$$profile || exit $$?; \
	done
else
	$(MAKE) -f makefiles/build.mk TARGET=amiga \
		TARGET_BUILD_DIR=build/amiga/$(AMIGA_PROFILE) \
		AMIGA_CRT=$(AMIGA_CRT_$(AMIGA_PROFILE)) \
		AMIGA_WB13=$(if $(filter wb13,$(AMIGA_PROFILE)),1,0) \
		FUJINET_NIO_DRIVER_BUILD=../fujinet-nio-driver/build/amiga/$(AMIGA_PROFILE)
endif

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
