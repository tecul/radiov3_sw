all:
	idf.py build
	pwd
	git describe --abbrev=8 --long --always
	python scripts/build_manifest.py

clean:
	idf.py clean

flash:
	idf.py flash -b 2000000

monitor:
	idf.py monitor
