sps: sps.c
	gcc -std=c99 -Wall -Wextra -g -o sps sps.c

clean:
	rm *.o sps
