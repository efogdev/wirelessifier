flash:
	cd main/web/front && bun run build
	idf.py build flash

erase:
	parttool.py erase_partition --partition-name nvs