BOOT_DISK_TARGETS := boot-disk boot-disk-$(PLATFORM) install-boot-disk

BOOT_DISK_DIR ?= boot-disk
BOOT_DISK_STAGE_DIR ?= $(DISK_DIR)/boot-stage
BOOT_DISK_OUTPUT_DIR ?= $(DISK_DIR)/boot
BOOT_DISK_MANIFEST ?= $(BOOT_DISK_DIR)/manifests/$(PLATFORM).yaml
BOOT_DISK_STAGE_MANIFEST ?= $(BOOT_DISK_DIR)/scripts/stage_manifest.py

FUJINET_NIO ?= ../fujinet-nio
BOOT_DISK_INSTALL_PLATFORM ?= $(PLATFORM)
FUJINET_NIO_POSIX_BOOT_DIR ?= $(FUJINET_NIO)/distfiles/boot/$(BOOT_DISK_INSTALL_PLATFORM)
FUJINET_NIO_ESP32_BOOT_DIR ?= $(FUJINET_NIO)/distfiles/esp32-data/boot/$(BOOT_DISK_INSTALL_PLATFORM)

BOOT_ATARI_IMAGE ?= $(BOOT_DISK_OUTPUT_DIR)/autorun.atr
BOOT_BBC_IMAGE ?= $(BOOT_DISK_OUTPUT_DIR)/FN-BOOT.ssd
BOOT_MSDOS_IMAGE ?= $(BOOT_DISK_OUTPUT_DIR)/autorun.img
BOOT_MSDOS_LABEL ?= FNBOOT
BOOT_MSDOS_PRESET ?= 1440

BOOT_DISK_IMAGE_atari := $(BOOT_ATARI_IMAGE)
BOOT_DISK_IMAGE_bbc := $(BOOT_BBC_IMAGE)
BOOT_DISK_IMAGE_msdos := $(BOOT_MSDOS_IMAGE)
BOOT_DISK_IMAGE := $(BOOT_DISK_IMAGE_$(PLATFORM))

define stage_boot_files
	rm -rf "$(BOOT_DISK_STAGE_DIR)"
	mkdir -p "$(BOOT_DISK_STAGE_DIR)"
	$(if $(filter bbc,$(PLATFORM)),python3 bbc/scripts/generate_config_nio_templates.py)
	BOOT_DISK_BIN="$(abspath $(BIN_DIR))" python3 "$(BOOT_DISK_STAGE_MANIFEST)" --manifest "$(BOOT_DISK_MANIFEST)" --stage "$(BOOT_DISK_STAGE_DIR)" --repo-root "$(CURDIR)"
endef

boot-disk: boot-disk-$(PLATFORM)

boot-disk-atari: all check-dir2atr | $(BOOT_DISK_OUTPUT_DIR) $(ATARI_CACHE_DIR)
	$(stage_boot_files)
	@if [ ! -f "$(ATARI_PICOBOOT)" ]; then \
		echo "Downloading picoboot.bin to $(ATARI_PICOBOOT)"; \
		curl -fsSL "$(PICOBOOT_DOWNLOAD_URL)" -o "$(ATARI_PICOBOOT)"; \
	fi
	rm -f "$(BOOT_ATARI_IMAGE)"
	$(DIR2ATR) $(ATARI_DIR2ATR_FLAGS) "$(BOOT_ATARI_IMAGE)" "$(BOOT_DISK_STAGE_DIR)"
	@echo "Created Atari boot disk: $(BOOT_ATARI_IMAGE)"

boot-disk-bbc: all | $(BOOT_DISK_OUTPUT_DIR)
	$(stage_boot_files)
	python3 "$(CREATE_SSD)" -i "$(BOOT_DISK_STAGE_DIR)" -o "$(BOOT_BBC_IMAGE)" -t FNBOOT
	@echo "Created BBC boot disk: $(BOOT_BBC_IMAGE)"

boot-disk-msdos: all | $(BOOT_DISK_OUTPUT_DIR)
	$(stage_boot_files)
	python3 msdos/scripts/create_msdos_img.py -i "$(BOOT_DISK_STAGE_DIR)" -o "$(BOOT_MSDOS_IMAGE)" -p "$(BOOT_MSDOS_PRESET)" -l "$(BOOT_MSDOS_LABEL)" --no-list
	@echo "Created MS-DOS boot disk: $(BOOT_MSDOS_IMAGE)"

install-boot-disk: boot-disk
	mkdir -p "$(FUJINET_NIO_POSIX_BOOT_DIR)"
	mkdir -p "$(FUJINET_NIO_ESP32_BOOT_DIR)"
	cp "$(BOOT_DISK_IMAGE)" "$(FUJINET_NIO_POSIX_BOOT_DIR)/"
	cp "$(BOOT_DISK_IMAGE)" "$(FUJINET_NIO_ESP32_BOOT_DIR)/"
	@echo "Installed boot disk to $(FUJINET_NIO_POSIX_BOOT_DIR)"
	@echo "Installed boot disk to $(FUJINET_NIO_ESP32_BOOT_DIR)"

$(BOOT_DISK_OUTPUT_DIR):
	mkdir -p $@
