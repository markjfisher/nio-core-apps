MSDOS_IMAGE ?= $(DISK_DIR)/nio-apps-msdos.img
MSDOS_STAGE_DIR ?= $(DISK_DIR)/stage
MSDOS_IMAGE_NAME_config-nio := CONFNIO.EXE
MSDOS_RETIRED_DISK_BINS := nioprobe.exe nioread.exe niodump.exe
MSDOS_DISK_BINS := $(filter-out $(MSDOS_RETIRED_DISK_BINS),$(notdir $(PROGRAM_BINS)))

define MSDOS_IMAGE_NAME
$(or $(MSDOS_IMAGE_NAME_$(basename $(1))),$(notdir $(1)))
endef

.PHONY: disk-msdos
disk-msdos: all | $(DISK_DIR)
	rm -rf "$(MSDOS_STAGE_DIR)"
	mkdir -p "$(MSDOS_STAGE_DIR)"
	$(foreach bin,$(MSDOS_DISK_BINS),cp "$(BIN_DIR)/$(bin)" "$(MSDOS_STAGE_DIR)/$(call MSDOS_IMAGE_NAME,$(bin))";)
	python3 msdos/scripts/create_msdos_img.py -i "$(MSDOS_STAGE_DIR)" -o "$(MSDOS_IMAGE)"

DISK_TARGETS += disk-msdos
