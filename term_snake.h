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

char read_key()
{
	int nread;
	char c;

	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(0, &read_fds);

	struct timeval timeout;
	timeout.tv_sec = 2.0;
	timeout.tv_usec = 0;

	int ret = select(1, &read_fds, NULL, NULL, &timeout);

	if(ret > 0)
	{
		while((nread = read(STDIN_FILENO, &c, 1) != 1))
		{
			if(nread == -1 && errno != EAGAIN) die("Read Key");
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
			g_data.screen[i][j] = '0';
		}
	}
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
