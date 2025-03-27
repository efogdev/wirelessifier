include .env
export
DEVICE := $(if $(WIRELESSIFIER_DEVICE),$(WIRELESSIFIER_DEVICE),$(shell ls /dev/ttyUSB* 2>/dev/null | head -n1))
ifeq ($(strip $(DEVICE)),)
$(error Error: add WIRELESSIFIER_DEVICE (as /dev/ttyUSB*) to the .env)
endif

.PHONY: build
build:
	cd main/web/front && bun run build
	idf.py build

flash: build
	- kill `ps aux | grep idf.py | cut -d' ' -s -f6`
	until esptool.py --chip esp32s3 -p "$(DEVICE)" -b 230400 --before=no_reset --after watchdog_reset write_flash --flash_mode dio --flash_freq 80m --flash_size detect 0x0 build/bootloader/bootloader.bin 0x10000 build/esp32s3-project.bin 0x8000 build/partition_table/partition-table.bin 0xf000 build/phy_init_data.bin; do \
		echo; \
	done
	idf.py monitor -p "$(DEVICE)" -b 230400

erase-nvs:
	parttool.py erase_partition --partition-name nvs
