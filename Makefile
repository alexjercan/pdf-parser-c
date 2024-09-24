.PHONY: clean

build:
	gcc main.c -o main -lz

clean:
	rm main
