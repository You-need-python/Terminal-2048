make: main.c
	gcc main.c -o main -Wall -Wextra -pedantic -std=c99

debug: main.c
	gcc main.c -o debug -g -Wall -Wextra -pedantic -std=c99

clean:
	rm main debug
