#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(key) (0x1f & (key))

/* represents an arrow keys, initialize
 * them with large integer that is out 
 * of the range of char, so they don't
 * conflict with any ordinary keypresses
 */
enum controlKey
{
	ARROW_UP = 1000,
	ARROW_DOWN,
	ARROW_RIGHT,
	ARROW_LEFT,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY
};

struct textRow
{
	int size;
	char *chars;
};

struct config
{
	int cx;
	int cy;
	int screenCols;
	int screenRows;
	int rowOffset;
	int colOffset;
	int rowNum;
	struct textRow* row;
	struct termios start_term;
};

struct vector
{
	char* v;
	int len;
};

struct config E;

void die_with_error(const char* msg)
{
	perror(msg);
	exit(-1);
}

void vector_append(struct vector* vec, const char* s, int length)
{
	char* temp = realloc(vec->v, vec->len + length);
	if (temp == NULL)
		return;

	memcpy(&temp[vec->len], s, length);
	vec->v = temp;
	vec->len += length;

	return;
}

void restore_back_to_start_settings()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.start_term);
}

void enable_raw_mode()
{
	// read terminal atributes into the start_term
	if (tcgetattr(STDIN_FILENO, &E.start_term) < 0)
		die_with_error("tcgetattr failed");

	// call this function when program is finished
	atexit(restore_back_to_start_settings);

	struct termios term = E.start_term;

	/* turn off the following FLAGs (FLAG - effect):
	 * ECHO 	- causes each key you type not to be displayed at the terminal
	 * ICANON 	- allows reading byte-by-byte instead of line-by-line
	 * ISIG 	- Ctrl+C and Ctrl+Z no longer work (SIGINT & SIGTSTP)
	 * IXON		- Ctrl+S and Ctrl+Q no longer work
	 * IEXTEN 	- Ctrl+V and Ctrl+O no longer work
	 * ICRNL 	- turn off feature that translates '\r' into the '\n'
	 *			- also Ctrl+M and Enter is now read as 13
	 * OPOST 	- turn off feature that translates '\n' into the '\r\n'
	 *			- newline characters are only moving cursor down and not to the left side of the screen
	 * CS8 		- this one we're not turning off but setting up: it sets the charachter size to 8 bits per byte
	 * OTHER	- miscellaneous flags that is usually alreay turned off
	 */
	term.c_cflag |= (CS8);
	term.c_iflag &= ~(IXON | BRKINT | ICRNL | INPCK | ISTRIP);
	term.c_oflag &= ~(OPOST);
	term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	
	/* VMIN sets the minimum amount of bytes of input needed before read() can return
	 * VTIME sets the maximum amount of time to wait before read() returns (1 = 100 milisec)
	 */
	term.c_cc[VMIN]  = 0;
	term.c_cc[VTIME] = 1;

	/* TCSAFLUSH waits for all pending output to be written
	 * and discards any input that hasn't been read
	 */
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) < 0)
		die_with_error("tcsetattr failed");
}

int get_cursor_position(int *rows, int* cols)
{
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int get_window_size(int *rows, int* cols)
{
	struct winsize ws;

	if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) || ws.ws_col == 0)
	{
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return get_cursor_position(rows, cols);
	}
	else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0; 
	}
}

void init()
{// initialize E struct
	E.cx = 0;
	E.cy = 0;
	E.screenCols = 0;
	E.screenRows = 0;
	E.rowOffset = 0;
	E.colOffset = 0;
	E.rowNum = 0;
	E.row = NULL;

	if (get_window_size(&E.screenRows, &E.screenCols) < 0)
		die_with_error("get_window_size failed");
	// printf("%d %d\n", E.screenCols, E.screenRows);
}

void append_row(char *line, size_t len)
{
	E.row = realloc(E.row, sizeof(struct textRow) * (E.rowNum + 1));

	int at = E.rowNum;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, line, len);
	E.row[at].chars[len] = '\0';
	E.rowNum++;
}

void open_editor(char *filename)
{// if user wants to open some file
	FILE *fp = fopen(filename, "r");
	if (!fp)
		die_with_error("fopen failed");

	char *line = NULL;
	// how much memory allocated
	size_t lineCapacity = 0;
	ssize_t lineLen = 0;

	// read lines from file and write them into E.row
	while ((lineLen = getline(&line, &lineCapacity, fp)) != -1)
	{
		/* strip of \r and \n from a line cause we know 
		 * that E.row represents one line of text
		 */
		while (lineLen > 0 && ((line[lineLen-1] == '\r' || line[lineLen-1] == '\n')))
			lineLen--;
		append_row(line, lineLen);
	}

	free(line);
	fclose(fp);
}

void scroll()
{
	/* checks if cursor is above the visible window
	 * and if so scrolls up to where the cursor is
	 */
	if (E.cy < E.rowOffset)
		E.rowOffset = E.cy;

	// checks if the cursor is past bottom of the visible window
	if (E.cy >= E.rowOffset + E.screenRows) 
		E.rowOffset = E.cy - E.screenRows + 1;

	if (E.cx < E.colOffset)
		E.colOffset = E.cx;

	if (E.cx >= E.colOffset + E.screenCols)
		E.colOffset = E.cx - E.screenCols + 1;
}

void draw_rows(struct vector* vec)
{// display E.row
	int y;

	// draw rows from the text buffer
	for (y = 0;y < E.screenRows; y++)
	{
		int fileRow = y + E.rowOffset;
		if (fileRow >= E.rowNum)
		{
			if (E.rowNum == 0 && y == E.screenRows / 3)
			{
				char welcome[80] = "KiLo Editor -- version 0.0.1";
				int welcomeLen = strlen(welcome);
				if (welcomeLen > E.screenCols) 
					welcomeLen = E.screenCols; 
				int padding = (E.screenCols - welcomeLen) / 2;
				if (padding)
				{
					vector_append(vec, "~", 1);
					padding--;
				}
				while (padding--)
					vector_append(vec, " ", 1);
				vector_append(vec, welcome, welcomeLen);
			}
			else
			{
				vector_append(vec, "~", 1);
			}
		}
		else
		{
			int len = E.row[fileRow].size - E.colOffset;
			if (len < 0)
				len = 0;
			if (len > E.screenCols)
				len = E.screenCols;

			vector_append(vec, &E.row[fileRow].chars[E.colOffset], len);
		}
			// erases the part of the line to the right of the cursor
		vector_append(vec, "\x1b[K", 3);
		if (y < E.screenRows - 1)
			vector_append(vec, "\r\n", 2);
	}

}

void refresh_screen()
{	
	struct vector vec = {NULL, 0};
	char buf[32];

	scroll();

	// hide the cursor while refreshing the screen
	vector_append(&vec, "\x1b[25l", 6);

	/* clearing the screen
	 * \x1b[2J - is an escape sequence - it's starts with
	 * \x1b - escape character (27 in dec) and follows by '['
	 * 2 - is an argument and 'J' - is a command, which in
	 * this case says to clear an entire screen
	 */ 
	// vector_append(&vec, "\x1b[2J", 4);

	// this escape sequence returns cursor back to the home position
	vector_append(&vec, "\x1b[H", 3);

	draw_rows(&vec);

	// moves the cursor to the (cx+1, cy+1) position
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOffset) + 1, (E.cx - E.colOffset) + 1);
	vector_append(&vec, buf, strlen(buf));

	// show the cursor again
	vector_append(&vec, "\x1b[?25h", 6);

	if (write(STDOUT_FILENO, vec.v, vec.len) < 0)
		die_with_error("write failed");

	free(vec.v);
}


int read_key()
{
	char c = '\0';
	int nread;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
		if (nread < 0 && errno != EAGAIN)
			die_with_error("read failed");
	
	/* if we read an escape character we immediately read two more bytes
	 * into the seq buffer to see if the escape sequence is an arrow key
	 * escape sequence
	 */ 
	if (c == '\x1b')
	{
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) 
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';


		if (seq[0] == '[')
		{
			/* PAGE_UP & PAGE_DOWN esc seq are \x1b[~5 and \x1b[~6
			 * HOME and KEY esc seq are \x1b[(1|7) and \x1b[(4|8)
			 */
			if (seq[1] >= '0' && seq[1] <= '9')
			{
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';

				if (seq[2] == '~')
				{
					switch(seq[1])
					{
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}
			else 
			{
				switch (seq[1])
				{
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		else if (seq[0] == 'O')
		{// HOME and KEY esc seq are \x1bOH and \x1bOF
			switch (seq[1])
			{
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b';
	}
	else
	{
		return c;
	}

}

void move_cursor(int key)
{
	/* check if the cursor is on an actual line, if so the 'row' 
	 * variable will point to the 'erow'that the cursor is on 
	 */
	struct textRow *row = (E.cy >= E.rowNum) ? NULL : &E.row[E.cy];

	switch (key)
	{
		case ARROW_UP:
			if (E.cy != 0)
				E.cy--;
			break;
		case ARROW_LEFT:
			if (E.cx != 0)
				E.cx--;
			else if (E.cy > 0)
			{/* if user presses left arrow at the start of the string
			  * move the cursor to the end of the previous string
			  */
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.rowNum)
				E.cy++;
			break;
		case ARROW_RIGHT:
				if (row && E.cx < row->size)
					E.cx++;
				else if (row && E.cx == row->size)
				{/* if the user presses right key at the end of a string
				  * move the cursor to the start of the next string
				  */
					E.cy++;
					E.cx = 0;
				}
			break;
	}

 	/* if we move the cursor down to the line which is shorter tha
 	 * the previous one we want it to be set past the last symbol of that line
 	 */
	row = (E.cy >= E.rowNum) ? NULL : &E.row[E.cy];
	int rowLen = row ? row->size : 0;
	if (E.cx > rowLen)
		E.cx = rowLen;


	return;
}

void process_keypress()
{
	int c = read_key();

	switch (c)
	{
		// quit button
		case CTRL_KEY('q'):
		{
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
		}
		
		case HOME_KEY: 
			E.cx = 0; 
			break;
		case END_KEY: 
			E.cx = E.screenCols - 1; 
			break;

		case PAGE_DOWN:
		case PAGE_UP:
		{
			int times = E.screenRows;
			while(--times)
				move_cursor(c == PAGE_DOWN ? ARROW_DOWN : ARROW_UP);
		}
		break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
		{
			move_cursor(c);
			break;
		}

		default:
			// iscntrl() checks wether a character is control characer (0-31,127 ASCII)
			if (iscntrl(c))
				printf("%d\r\n", c);
			else
				printf("%d '%c'\r\n", c, c);
		
	}

	return;
}

int main(int argc, char *argv[])
{
	enable_raw_mode();
	init();
	
	if (argc >= 2)
		open_editor(argv[1]);

	while (1)
	{
		refresh_screen();

		process_keypress();
	}
	return 0;
}

/* Ctrl+Z - suspend the program to the background (sends SIGTSTP signal); fg - bring it back to the foreground
 * Ctrl+C - sends SIGINT signal; Ctrl+S - stops data from being transmitted to the terminal until you press Ctrl+Q  
 * Ctrl+V - causes terminal to wait for you to type another character; Ctrl+O - in MacOS terminal discards this character
 * sequnces starting with '\x1b' are escape sequences, they are instructing terminal to do various 
 * text formatting tasks, such as coloring text, moving cursor around clearing parts of the screen 
 */