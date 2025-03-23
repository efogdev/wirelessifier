build:
	cd main/web/front && bun run build
	idf.py build

flash:
	cd main/web/front && bun run build
	idf.py build flash

erase-nvs:
	parttool.py erase_partition --partition-name nvs
