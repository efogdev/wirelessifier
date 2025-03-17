flash:
	cd main/web/front && bun run build
	idf.py build flash
