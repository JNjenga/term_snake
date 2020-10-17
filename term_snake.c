#include "term_snake.h"

int main(void)
{
	init();

	while(1)
	{
		render();
		handle_input();
	}

	exit_app();
	return 0;
}
