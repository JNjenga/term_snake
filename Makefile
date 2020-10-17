tem_snake: term_snake.c
	$(CC) term_snake.c -o term_snake -Wall -Wextra -pedantic -std=c99


run: term_snake.c # assuming $(PROG) is the name of your program
	$(CC) term_snake.c -o term_snake -Wall -Wextra -pedantic -std=c99 && ./term_snake.exe

.PHONY: run 
