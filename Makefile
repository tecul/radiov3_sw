all:
	idf.py build
	python3 scripts/build_manifest.py

clean:
	idf.py clean

flash:
	idf.py flash -b 2000000

monitor:
	idf.py monitor
