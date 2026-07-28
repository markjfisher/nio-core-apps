ATARI_STAGE_DIR ?= $(DISK_DIR)/stage
ATARI_CACHE_DIR ?= $(TARGET_BUILD_DIR)/cache/atari
ATARI_IMAGE ?= $(DISK_DIR)/nio-apps-atari.atr
DIR2ATR ?= dir2atr
PICOBOOT_DOWNLOAD_URL ?= https://github.com/FujiNetWIFI/assets/releases/download/picobin/picoboot.bin
ATARI_PICOBOOT ?= $(ATARI_CACHE_DIR)/picoboot.bin
ATARI_DIR2ATR_FLAGS ?= -m -D -B $(ATARI_PICOBOOT)

.PHONY: disk-atari check-dir2atr
disk-atari: all check-dir2atr | $(DISK_DIR) $(ATARI_CACHE_DIR)
	rm -rf $(ATARI_STAGE_DIR)
	mkdir -p $(ATARI_STAGE_DIR)
	cp $(BIN_DIR)/*.xex $(ATARI_STAGE_DIR)/
	@echo "Staged Atari executables in $(ATARI_STAGE_DIR)"
	@if [ ! -f "$(ATARI_PICOBOOT)" ]; then \
		echo "Downloading picoboot.bin to $(ATARI_PICOBOOT)"; \
		curl -fsSL "$(PICOBOOT_DOWNLOAD_URL)" -o "$(ATARI_PICOBOOT)"; \
	fi
	rm -f $(ATARI_IMAGE)
	$(DIR2ATR) $(ATARI_DIR2ATR_FLAGS) $(ATARI_IMAGE) $(ATARI_STAGE_DIR)
	@echo "Created Atari ATR image: $(ATARI_IMAGE)"

check-dir2atr:
	@command -v $(DIR2ATR) >/dev/null 2>&1 || { \
		echo ""; \
		echo "ERROR: dir2atr is required to create Atari ATR disk images."; \
		echo "Install it from https://github.com/HiassofT/AtariSIO or set DIR2ATR=/path/to/dir2atr."; \
		echo ""; \
		exit 1; \
	}

$(ATARI_CACHE_DIR):
	mkdir -p $@

DISK_TARGETS += disk-atari
