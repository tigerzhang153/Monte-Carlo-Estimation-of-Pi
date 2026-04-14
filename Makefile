all: monte_carlo

monte_carlo: monte_carlo.c monte_carlo.h
	gcc -Wall -g -o monte_carlo monte_carlo.c -lm

run: monte_carlo
	./monte_carlo

clean:
	rm monte_carlo