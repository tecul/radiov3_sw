all:
	idf.py build

clean:
	idf.py clean

flash:
	idf.py flash -b 2000000

monitor:
	idf.py monitor
