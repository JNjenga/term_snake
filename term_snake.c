#include "term_snake.h"

int main(void)
{
	init();


	while(1)
	{

		render();
		handle_input();
	}
	
	disable_rawmode();
	exit_app();

	return 0;
}
