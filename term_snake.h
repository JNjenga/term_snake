#include <ctype.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/select.h>

/******** DEFINES SECTION ********************/

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

#define SNAKE_FOOD_CHAR '@'
#define SNAKE_HEAD_CHAR 'x'
#define SNAKE_BODY_CHAR '+'

/******** DATA SECTION ********************/

enum keys
{
	ARROW_UP = 1000,
	ARROW_DOWN,
	ARROW_RIGHT,
	ARROW_LEFT,
};

struct window_data
{
	int rows;
	int cols;
	struct termios o_state; // Original state of the terminal
} E;

struct abuf
{
	char *b;
	int len;
};

struct game_data
{
	char **screen;	// Window data
	int hr, hc;		// Snake head, (row , column)
	int grow;
	int length;		// Snake length
	int dir;		// Snake direction
	int score;		// Game score
	int pause;		// Game state 
}g_data;

/******** TERMINAL SECTIONS ********************/

void die(const char * error)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(error);

	exit(1);
}

void disable_rawmode()
{
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.o_state) == -1)
		die("tcsetattr");
}

void enable_rawmode()
{
	if(tcgetattr(STDIN_FILENO, &E.o_state) == -1)
		die("tcgetattr");

	atexit(disable_rawmode);

	struct termios raw = E.o_state;

	tcgetattr(STDIN_FILENO, &raw);
		// Read current attributes into $raw struct
	tcgetattr(STDIN_FILENO, &raw);
	// Modify the struct, disable ECHO 
	raw.c_iflag &= ~( BRKINT | ICRNL | INPCK | ISTRIP | IXON );
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	
	// Apply the modified attribute
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

int read_key()
{
	int nread;
	char c;

	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(0, &read_fds);

	struct timeval timeout;
	timeout.tv_sec = 1.0;
	timeout.tv_usec = 0.0;

	int ret = select(1, &read_fds, NULL, NULL, &timeout);

	if(ret > 0)
	{
		while((nread = read(STDIN_FILENO, &c, 1) != 1))
		{
			if(nread == -1 && errno != EAGAIN) die("Read Key");
		}

		if(c == '\x1b')
		{
			char seq[3];

			if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
			if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

			if(seq[0] == '[')
			{
				if(seq[1])
				{
					switch(seq[1])
					{
						case 'A' : return ARROW_UP;
						case 'B' : return ARROW_DOWN;
						case 'C' : return ARROW_RIGHT;
						case 'D' : return ARROW_LEFT;
					}
				}
			}
		}
	}

	return c;
}

int get_cursor_position(int *rows,int *cols)
{
	char buf[32];
	unsigned int i = 0;

	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while(i < sizeof(buf) -1)
	{
		if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if(buf[i] == 'R') break;
		i++;
	}

	buf[i] = '\0';
	
	if(buf[0] != '\x1b' || buf[1] != '[') return -1;
	if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int get_window_size(int * rows,int * cols)
{
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 1 || ws.ws_col == 0)
	{
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return get_cursor_position(rows, cols);
	}else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;

		return 0;
	}
}

void ab_append(struct abuf * ab, const char *s, int len)
{
	char * new_c = realloc(ab->b, ab->len + len);
	if(new_c == NULL) return;

	memcpy(&new_c[ab->len], s, len);
	ab->b = new_c;
	ab->len += len;
}

void ab_free(struct abuf * ab)
{
	free(ab->b);
}

/******** GAME SECTION ********************/

void spwan_food()
{
	unsigned int r = rand() % (E.rows - 3) + 1;
	unsigned int c = rand() % (E.cols - 2) + 1;
	
	while(g_data.screen[r][c] != 0)
	{
		r = rand() % (E.rows - 2) + 1;
		c = rand() % (E.cols - 2) + 1;
	}

	g_data.screen[r][c] = SNAKE_FOOD_CHAR;
}

 /* Moves the snake to the next step recursively */

void move_rec(int length, int next_r, int next_c,
		int current_r, int current_c)
{
	if(length < 0) {
		if(g_data.grow)
		{
			g_data.screen[current_r][current_c] = SNAKE_BODY_CHAR;
			g_data.length ++;
			g_data.grow = 0;
		} else
		{
			g_data.screen[current_r][current_c] = 0;
		}
		return;
	}

	length--;
	
	// If you want to handle collisions, do it here
	g_data.screen[next_r][next_c] = g_data.screen[current_r][current_c];
	

	if((g_data.screen[current_r][current_c+1] == SNAKE_HEAD_CHAR
			|| g_data.screen[current_r][current_c+1] == SNAKE_BODY_CHAR )
			&& (current_c+1) != next_c)	// Rightside 
	{
		next_r = current_r; next_c = current_c;
		current_c++;
	} 
	else if((g_data.screen[current_r+1][current_c] == SNAKE_HEAD_CHAR
			|| g_data.screen[current_r+1][current_c] == SNAKE_BODY_CHAR )
			&& (current_r+1) != next_r)	// Downside 
	{
		next_r = current_r; next_c = current_c;
		current_r++;
	}  else if((g_data.screen[current_r][current_c-1] == SNAKE_HEAD_CHAR
			|| g_data.screen[current_r][current_c-1] == SNAKE_BODY_CHAR)
			&& (current_c-1) != next_c)		// Leftside
	{
		next_r = current_r; next_c = current_c;
		current_c--;
	}else if(next_r == 0){
		if(g_data.screen[current_r][current_c-1] == SNAKE_HEAD_CHAR
				|| g_data.screen[current_r][current_c-1] == SNAKE_BODY_CHAR)
		{
			next_r = current_r; next_c = current_c;
			current_c --;
		}else{
			next_r = current_r; next_c = current_c;
			current_c++;
		}
	}
	else if((g_data.screen[current_r-1][current_c] == SNAKE_HEAD_CHAR ||
				g_data.screen[current_r-1][current_c] == SNAKE_BODY_CHAR)
				 && (current_r-1) != next_r)	// Upside 
	{
		next_r = current_r; next_c = current_c;
		current_r--;
	}

	move_rec(length, next_r, next_c, current_r, current_c);
}

void tick()
{
	int current_r = g_data.hr, current_c = g_data.hc;
	int next_r = current_r , next_c = current_c;

	switch(g_data.dir) {
		case UP :
			next_r--;
			break;
		case DOWN:
			next_r++;
			break;
		case LEFT:
			next_c--;
			break;
		case RIGHT:
			next_c++;
			break;
		default:
			break;
	}

	if(next_r < 2 || next_c < 1 || next_r > E.rows - 3 || next_c > E.cols - 2)
	{
		return;
	}
	
	if(g_data.screen[next_r][next_c] == SNAKE_FOOD_CHAR) 
	{
		spwan_food();
		g_data.score ++;
		g_data.screen[next_r][next_c] = 0;
		g_data.grow = 1;
	}
	g_data.hr = next_r; g_data.hc = next_c;

	move_rec(g_data.length, next_r, next_c, current_r, current_c);

}



void init()
{
	enable_rawmode();

	srand(time(NULL));

	write(STDOUT_FILENO, "\x1b[2J", 4);		// Clear screen
	write(STDOUT_FILENO, "\x1b[?25l", 6);	// Hide cursor
	
	get_window_size(&E.rows, &E.cols);

	g_data.screen = malloc(E.rows * sizeof * g_data.screen);

	for(int i = 0; i < E.rows; i++)
	{
		g_data.screen[i] = malloc(E.cols * sizeof * g_data.screen[i]);
	}

	for(int i = 0; i < E.rows; i++)
	{
		for(int j = 0; j < E.cols; j++)
		{
			g_data.screen[i][j] = 0;
		}
	}
	
	g_data.pause = 0;

	int hr = E.rows/2; 
	int hc = E.cols/2;

	g_data.dir = UP;
	g_data.length = 2;

	g_data.hr = E.rows/2;
	g_data.hc = E.cols/2;

	g_data.screen[hr][hc] = SNAKE_HEAD_CHAR;
	g_data.screen[hr][hc-1] = SNAKE_BODY_CHAR;
	g_data.screen[hr][hc-2] = SNAKE_BODY_CHAR;

	g_data.score = 0;

	spwan_food();
}

void render()
{
	struct abuf ab = {NULL, 0};
	
	ab_append(&ab, "\x1b[0;0H", 6);
	ab_append(&ab, "\x1b[0K", 4);

	if(g_data.pause != 1)
	{
		tick();

		for(int i = 0; i < E.rows; i++)
		{
			for(int j = 0; j < E.cols; j++)
			{
				if(i == 1 ||i == E.rows-2 )
					ab_append(&ab, "#", 1);
				else if( j == 0 ||  j == E.cols-1)
					ab_append(&ab, "*", 1);
				else if(g_data.screen[i][j] == 0)
					ab_append(&ab, " ", 1);
				else
					ab_append(&ab, &g_data.screen[i][j], 1);
			}
		}

	}

	ab_append(&ab, "\x1b[0;0H", 6);

	char buf[60];

	int len = snprintf(buf, 23, "SCORE : [%d] %s", g_data.score, (g_data.pause) ? "PAUSED" : "NOT PAUSED");

	ab_append(&ab, buf, len);
	
	// char last_row[10];
	len = snprintf(buf, 58, "\x1b[%d;0H <Q> : Quit, <P> : Pause/help, <ARROWS> : Movements", E.rows);
	
	ab_append(&ab, buf, len);

	write(STDOUT_FILENO, ab.b, ab.len);
	ab_free(&ab);
}

void exit_app()
{
	for(int i = 0; i < E.rows; i++)
	{
		free(g_data.screen[i]);
	}

	free(g_data.screen);

	write(STDOUT_FILENO, "\x1b[2J", 4);	
	write(STDOUT_FILENO, "\x1b[?25h", 6);
}

void handle_input()
{
	switch(read_key())
	{
		case 'q':
			disable_rawmode();
			exit_app();
			exit(0);
			break;
		case ARROW_UP:
			g_data.dir = UP;
			break;

		case ARROW_DOWN:
			g_data.dir = DOWN;
			break;

		case ARROW_LEFT:
			g_data.dir = LEFT;
			break;

		case ARROW_RIGHT:
			g_data.dir = RIGHT;
			break;
		case 'p':
			if(g_data.pause == 1)
				g_data.pause = 0;
			else
				g_data.pause = 1;
			break;
		default:
			break;
	}
}

