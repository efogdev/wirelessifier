build:
	cd main/web/front && bun run build
	idf.py build flash

flash:
	make build
	idf.py flash

erase:
	parttool.py erase_partition --partition-name nvs