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

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

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
	char **screen;
	int hr, hc;
	int length;
	int dir;
}g_data;

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
	timeout.tv_sec = 0.9;
	timeout.tv_usec = 0.9;

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

/*
 * Moves the snake to the next step
 *
 */
void move_rec(int length, int next_r, int next_c,
		int current_r, int current_c)
{	
	if(length < 0) {
		g_data.screen[current_r][current_c] = 0;
		return;
	}

	length--;
	
	// If you want to handle collisions, do it here
	g_data.screen[next_r][next_c] = g_data.screen[current_r][current_c];

	if(g_data.screen[current_r][current_c-1] != 0 && (current_c-1) != next_c)		// Leftside
	{
		next_r = current_r; next_c = current_c;
		current_c--;
	} else if(g_data.screen[current_r][current_c+1] != 0 && (current_c+1) != next_c)	// Rightside 
	{
		next_r = current_r; next_c = current_c;
		current_c++;
	} else if(g_data.screen[current_r-1][current_c] != 0 && (current_r-1) != next_r)	// Upside 
	{
		next_r = current_r; next_c = current_c;
		current_r--;
	} else if(g_data.screen[current_r+1][current_c] != 0 && (current_r+1) != next_r)	// Downside 
	{
		next_r = current_r; next_c = current_c;
		current_r++;
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

	g_data.hr = next_r; g_data.hc = next_c;

	move_rec(g_data.length, next_r, next_c, current_r, current_c);

	// print_snake();
	// printf("r : %d, c : %d, nr : %d, nc : %d\n", current_r, current_c, next_r, next_c);
}

void init()
{
	enable_rawmode();
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[?25l", 6);
	
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
	
	int hr = E.rows/2; 
	int hc = E.cols/2;

	g_data.dir = UP;
	g_data.length = 2;

	g_data.hr = E.rows/2;
	g_data.hc = E.cols/2;

	g_data.screen[hr][hc] = 'x';
	g_data.screen[hr][hc-1] = '+';
	g_data.screen[hr][hc-2] = '+';
}

void render()
{
	struct abuf ab = {NULL, 0};
	
	ab_append(&ab, "\x1b[0;0H", 6);
	ab_append(&ab, "\x1b[0K", 4);

	for(int i = 0; i < E.rows; i++)
	{
		for(int j = 0; j < E.cols; j++)
		{
			if(g_data.screen[i][j] == 0)
				ab_append(&ab, " ", 1);
			else
				ab_append(&ab, &g_data.screen[i][j], 1);
		}

		// ab_append(&ab, "\r\n", 2);
	}


	write(STDOUT_FILENO, ab.b, ab.len);
	ab_free(&ab);
}

void update()
{
}

void handle_input()
{
	switch(read_key())
	{
		case 'q':
			disable_rawmode();
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
		default:
			break;
	}
}

void exit_app()
{
	for(int i = 0; i < E.rows; i++)
	{
		free(g_data.screen[i]);
	}

	free(g_data.screen);
	write(STDOUT_FILENO, "\x1b[?25h", 6);	
}
