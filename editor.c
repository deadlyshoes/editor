#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	CTRL_ARROW_LEFT,
	CTRL_ARROW_RIGHT,
	SHIFT_ARROW_LEFT,
	SHIFT_ARROW_RIGHT,
	SHIFT_ARROW_UP,
	SHIFT_ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

enum editorHightlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/* data */
struct editorSyntax {
	char *filetype;
	char **filematch;
	char **keywords;
	char *singleline_comment_start;
	char *multiline_comment_start;
	char *multiline_comment_end;
	int flags;
};

typedef struct erow {
	int idx;
	int size;
	int rsize;
	char *chars;
	char *render;
	unsigned char *hl;
	int hl_open_comment;
	bool damaged; // redraw line
} erow;

struct editorConfig {
	int cx, cy;
	int rx;
	int rowoff, coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax *syntax;
	struct termios orig_termios;
};

struct editorConfig E;

/* filetypes */
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"struct", "union", "typedef", "static", "enum", "class", "case",
	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|", NULL
};

struct editorSyntax HLDB[] = {
	{
		"c",
		C_HL_extensions,
		C_HL_keywords,
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorProcessKeypress(int key);
int getWindowSize(int *rows, int *cols);

/* terminal */
void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tsetattr");
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}

	if (c == '\x1b') {
		char seq[5];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1':
							return HOME_KEY;
						case '3':
							return DEL_KEY;
						case '4':
							return END_KEY;
						case '5':
							return PAGE_UP;
						case '6':
							return PAGE_DOWN;
						case '7':
							return HOME_KEY;
						case '8':
							return END_KEY;
					}
				} else if (seq[2] == ';') {
					if (read(STDIN_FILENO, &seq[3], 1) == -1)
						return '\x1b';
					if (read(STDIN_FILENO, &seq[4], 1) == -1)
						return '\x1b';
					if (seq[3] == '5')
						switch (seq[4]) {
							case 'C':
								return CTRL_ARROW_RIGHT;
							case 'D':
								return CTRL_ARROW_LEFT;
						}
					else if (seq[3] == '2')
						switch (seq[4]) {
							case 'A':
								return SHIFT_ARROW_UP;
							case 'B':
								return SHIFT_ARROW_DOWN;
							case 'C':
								return SHIFT_ARROW_RIGHT;
							case 'D':
								return SHIFT_ARROW_LEFT;
						}
				}
			} else {
				switch (seq[1]) {
					case 'A':
						return ARROW_UP;
					case 'B':
						return ARROW_DOWN;
					case 'C':
						return ARROW_RIGHT;
					case 'D':
						return ARROW_LEFT;
					case 'H':
						return HOME_KEY;
					case 'F':
						return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
			}
		}

		return '\x1b';
	}
	return c;
}

void handleWindowResize(int sig) {
	signal(SIGWINCH, SIG_IGN);

	E.rowoff = 0;
	E.coloff = 0;
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows -= 2;
	editorRefreshScreen();

	signal(SIGWINCH, handleWindowResize);
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) == -1)
			break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}	

/* syntax hightlighting */
int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
	row->hl = realloc(row->hl, row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL)
		return;

	char **keywords = E.syntax->keywords;

	char *scs = E.syntax->singleline_comment_start;
	char *mcs = E.syntax->multiline_comment_start;
	char *mce = E.syntax->multiline_comment_end;

	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

		if (scs_len && !in_string && !in_comment)
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->rsize - i);
				break;
			}

		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				} else {
					i++;
					continue;
				}
			} else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}
				if (c == in_string)
					in_string = 0;
				i++;
				prev_sep = 1;
				continue;
			} else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if (isdigit(c) && (prev_sep || prev_hl == HL_NUMBER) ||
					(c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) {
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';
				if (kw2) klen--;

				if (!strncmp(&row->render[i], keywords[j], klen) &&
						is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}
			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}
		}

		prev_sep = is_separator(c);
		i++;
	}

	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->idx + 1 < E.numrows)
		editorUpdateSyntax(&E.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
	switch (hl) {
		case HL_COMMENT:
		case HL_MLCOMMENT:
			return 36;
		case HL_KEYWORD1:
			return 33;
		case HL_KEYWORD2:
			return 32;
		case HL_STRING:
			return 35;
		case HL_NUMBER:
			return 31;
		case HL_MATCH:
			return 34;
		default:
			return 37;
	}
}	

void editorSelectSyntaxHighlight() {
	E.syntax = NULL;
	if (E.filename == NULL)
		return;

	char *ext = strrchr(E.filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax *s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
				(!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;

				int filerow;
				for (filerow = 0; filerow < E.rowoff; filerow++)
					editorUpdateSyntax(&E.row[filerow]);
				for (; filerow < E.rowoff + E.screenrows; filerow++) {
					editorUpdateSyntax(&E.row[filerow]);
					E.row[filerow].damaged = true;
				}
				for (; filerow < E.numrows; filerow++)
					editorUpdateSyntax(&E.row[filerow]);
				return;
			}
			i++;
		}
	}
}

/* row operations */
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int old_rx;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		old_rx = cur_rx;
		if (row->chars[cx] == '\t')
			cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
		cur_rx++;

		if (cur_rx > rx) {
			if (rx - old_rx > cur_rx - rx)
				return cx + 1;
			return cx;
		}
	}
	return cx;
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t')
			tabs++;

	free(row->render);
	row->render = malloc(row->size + (KILO_TAB_STOP - 1) * tabs + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % KILO_TAB_STOP != 0)
				row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	row->damaged = true;

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
	if (at < 0 || at > E.numrows) return;

	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	int j;
	for (j = at + 1; j - E.rowoff < E.screenrows && j <= E.numrows; j++) {
		E.row[j].idx++;
		E.row[j].damaged = true;
	}
	for (; j <= E.numrows; j++)
		E.row[j].idx++;

	E.row[at].idx = at;
	
	E.row[at].size = len;
	E.row[at].chars = malloc((len + 1) * sizeof(char));
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	E.row[at].hl_open_comment = 0;

	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}	

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows)
		return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	int j;
	for (j = at; j - E.rowoff < E.screenrows && j < E.numrows - 1; j++) {
		E.row[j].idx--;
		E.row[j].damaged = true;
	}
	for (; j < E.numrows - 1; j++)
		E.row[j].idx--;
	E.numrows--;
	E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size)
		return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChars(erow *row, int at, int until) {
	if (at < 0 || at >= row->size)
		return;
	memmove(&row->chars[until], &row->chars[at + 1], row->size - at);
	row->size -= at + 1 - until;
	editorUpdateRow(row);
	E.dirty++;
}

/* editor operations */
void editorInsertChar(int c) {
	if (E.cy == E.numrows)
		editorInsertRow(E.numrows, "", 0);
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewline() {
	int il = 0; // indentation level to smart-indent
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
	} else {
		erow *row = &E.row[E.cy];

		/* smart-indent */
		char indented[row->size];
		for (il = 0; il < E.cx && (row->chars[il] == '\t' || row->chars[il] == ' '); il++)
			indented[il] = row->chars[il];
		memcpy(&indented[il], &row->chars[E.cx], row->size - E.cx);

		editorInsertRow(E.cy + 1, indented, il + row->size - E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
	for (int j = 0; j < il; j++)
		editorMoveCursor(ARROW_RIGHT);
}

void editorDelChar() {
	if (E.cy == E.numrows)
		return;
	if (E.cx == 0 && E.cy == 0)
		return;
	erow *row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1);
		E.cx--;
	} else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}

/* file io */
char *editorRowsToString(int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < E.numrows; j++)
		totlen += E.row[j].size + 1;
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);

	editorSelectSyntaxHighlight();

	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if (E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s", NULL);
		if (E.filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	int len;
	char *buf = editorRowsToString(&len);

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/* find */
void editorFindCallback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char *saved_hl = NULL;

	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		E.row[E.cy].damaged = true;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1)
		direction = 1;
	int current = last_match;
	for (int i = 0; i < E.numrows; i++) {
		current += direction;
		if (current == -1)
			current = E.numrows - 1;
		else if (current == E.numrows)
			current = 0;

		erow *row = &E.row[current];
		char *match = strstr(row->render, query);
		if (match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;

			saved_hl_line = current;
			saved_hl = malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char *query = editorPrompt("Search %s (Use ESC/Arrows/Enter)", editorFindCallback);

	if (query) {
		free(query);
	} else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}

/* jump */
void editorJump() {
	char *sline = editorPrompt("Jump to line: %s", NULL);
	int sline_len = strlen(sline);

	/* Poor workaround to treat VERY big line numbers */
	if (sline_len > 9)
		sline_len = 9;

	/* Avoiding str to (long...) int functions for now */
	int line = 0;
	for (int i = 0; i < sline_len; i++) {
		if (!isdigit(sline[i])) {
			editorSetStatusMessage("Type only digits!");
			return;
		}

		line = line * 10 + (sline[i] - '0');
	}

	if (line == 0) line = 1;
	E.cy = (line > E.numrows ? E.numrows : line) - 1; // E.cy starts at 0
	E.rowoff = E.numrows;
	editorSetStatusMessage("");
}

/* append buffer */
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, (ab->len + len) * sizeof(char));

	if (new == NULL)
		return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/* output */
bool editorScroll() {
	int cur_rowoff = E.rowoff;
	int cur_coloff = E.coloff;

	E.rx = 0;
	if (E.cy < E.numrows)
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

	if (E.cy < E.rowoff)
		E.rowoff = E.cy;
	if (E.cy >= E.rowoff + E.screenrows)
		E.rowoff = E.cy - E.screenrows + 1;

	if (E.rx < E.coloff)
		E.coloff = E.rx;
	if (E.rx >= E.coloff + E.screencols)
		E.coloff = E.rx - E.screencols + 1;

	return (cur_rowoff != E.rowoff || cur_coloff != E.coloff);
}

void editorDrawRows(struct abuf *ab) {
	for (int y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			char position[32];
			int position_len = snprintf(position, sizeof(position), "\x1b[%d;1H", y + 1);
			abAppend(ab, position, position_len);
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), 
						"Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols)
					welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				while (padding--)
					abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 3);
			}
			abAppend(ab, "\x1b[K", 3);
		} else {
			if (!E.row[filerow].damaged)
				continue;
			E.row[filerow].damaged = false;
			char position[32];
			int position_len = snprintf(position, sizeof(position), "\x1b[%d;1H", y + 1);
			abAppend(ab, position, position_len);
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0)
				len = 0;
			if (len > E.screencols)
				len = E.screencols;
			char *c = &E.row[filerow].render[E.coloff];
			unsigned char *hl = &E.row[filerow].hl[E.coloff];
			int current_color = -1;
			for (int j = 0; j < len; j++) {
				if (iscntrl(c[j])) {
					char sym = (c[j] <= 26) ? '@' + c[j] : '?';
					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &sym, 1);
					if (current_color != -1) {
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b]%dm", current_color);
						abAppend(ab, buf, clen);
					} else {
						abAppend(ab, "\x1b[m", 3);
					}
				} else if (hl[j] == HL_NORMAL) {
					if (current_color != -1) {
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				} else if (hl[j] == HL_MATCH) {
					if (current_color != HL_MATCH) {
						abAppend(ab, "\x1b[7m", 4);
						current_color = HL_MATCH;
					}
					abAppend(ab, &c[j], 1);
					if (j < len)
						if (hl[j + 1] != HL_MATCH)
							abAppend(ab, "\x1b[m", 3);
				} else {
					int color = editorSyntaxToColor(hl[j]);
					if (color != current_color) {
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						abAppend(ab, buf, clen);
						current_color = color;
					}
					abAppend(ab, &c[j], 1);
				}
			}
			abAppend(ab, "\x1b[39m", 5);
		}

		abAppend(ab, "\x1b[m", 3);
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	char position[32];
	int position_len = snprintf(position, sizeof(position), "\x1b[%d;1H", E.screenrows + 1);
	abAppend(ab, position, position_len);

	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
			E.filename ? E.filename : "[NO NAME]", E.numrows, E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
	if (len > E.screencols)
		len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
	char position[32];
	int position_len = snprintf(position, sizeof(position), "\x1b[%d;1H", E.screenrows + 2);
	abAppend(ab, position, position_len);

	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
	if (editorScroll())
		for (int i = 0; i < E.screenrows; i++)
			E.row[i + E.rowoff].damaged = true;

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);	
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/* input */
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
	size_t bufsize = 128;
	char *buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0)
				buf[--buflen] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback)
				callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback)
					callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback)
			callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	static int keep_rx = 0; // Maintain cursor position on row change
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			if (row)
				keep_rx = editorRowCxToRx(row, E.cx);
			break;
		case ARROW_RIGHT: 
			if (row && E.cx < row->size) {
				E.cx++;
			} else if (row && E.cx == row->size) {
				E.cy++;
				E.cx = 0;
			}
			if (row)
				keep_rx = editorRowCxToRx(row, E.cx);
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cx = editorRowRxToCx(&E.row[E.cy - 1], keep_rx);
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) {
				E.cx = editorRowRxToCx(&E.row[E.cy + 1], keep_rx);
				E.cy++;
			}
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen)
		E.cx = rowlen;
}

void editorMoveSelect(int key) {
	erow *row = &E.row[E.cy];

	switch (key) {
		case SHIFT_ARROW_LEFT:
			{
				if (E.cx > 0) {
					int left = editorRowCxToRx(row, E.cx - 1);
					int right;
					if (row->chars[E.cx - 1] == '\t')
						right = left + ((KILO_TAB_STOP - 1) - (left % KILO_TAB_STOP)) + 1;
					else
						right = left + 1;
					for (int i = left; i < right; i++)
						row->hl[i] = row->hl[i] == HL_MATCH ? HL_NORMAL : HL_MATCH;
					row->damaged = true;
				}
				editorMoveCursor(ARROW_LEFT);
			}
			break;
		case SHIFT_ARROW_RIGHT:
			{
				if (E.cx < row->size) {
					int left = editorRowCxToRx(row, E.cx);
					int right;
					if (row->chars[E.cx] == '\t')
						right = left + ((KILO_TAB_STOP - 1) - (left % KILO_TAB_STOP)) + 1;
					else
						right = left + 1;
					for (int i = left; i < right; i++)
						row->hl[i] = row->hl[i] == HL_MATCH ? HL_NORMAL : HL_MATCH;
					row->damaged = true;
				}
				editorMoveCursor(ARROW_RIGHT);
				//if (E.cy >= E.numrows)
				//		editorMoveCursor(ARROW_LEFT);
			}
			break;
		case SHIFT_ARROW_DOWN:
			{
				int until_end = row->size - E.cx;
				int until_below = editorRowRxToCx(&E.row[E.cy + 1], editorRowCxToRx(row, E.cx)); 
				int until = until_end + until_below;
				for (int i = 0; i <= until; i++)
					editorMoveSelect(SHIFT_ARROW_RIGHT);
			}
			break;
		case SHIFT_ARROW_UP:
			{
				int until_begin = E.cx;
				int until_above = editorRowRxToCx(&E.row[E.cy - 1], editorRowCxToRx(row, row->size - E.cx));
				int until = until_begin + until_above;
				for (int i = 0; i <= until; i++)
					editorMoveSelect(SHIFT_ARROW_LEFT);
			}
			break;
	}
}

void editorSelect(int key) {
	int start_y = E.cy;
	editorMoveSelect(key);

	while (1) {
		editorRefreshScreen();
		int c = editorReadKey();

		switch (c) {
			case SHIFT_ARROW_LEFT:
			case SHIFT_ARROW_RIGHT:
			case SHIFT_ARROW_UP:
			case SHIFT_ARROW_DOWN:
				editorMoveSelect(c);
				break;

			case BACKSPACE:
			case CTRL_KEY('h'):
			case DEL_KEY:
				{
					int up, down;
					if (E.cy < start_y) {
						up = E.cy;
						down = start_y;
					} else {
						up = start_y;
						down = E.cy;
					}
					if (up == down) {
						int i, j; // begin, end
						erow *row = &E.row[E.cy];
						for (i = 0; row->hl[i] != HL_MATCH && i < row->rsize; i++);
						for (j = i; row->hl[j] == HL_MATCH && j < row->rsize; j++);
						editorRowDelChars(row, j - 1, i);
						E.cx = editorRowRxToCx(row, i);
					} else {
						E.cy = down;
						erow *row = &E.row[E.cy];
						int k;
						for (k = row->rsize - 1; row->hl[k] != HL_MATCH && k > 0; k--);
						if (k == row->size - 1)
							editorDelRow(E.cy);
						else
							editorRowDelChars(row, k, 0);
						for (int i = down - 1; i > up; i--)
							editorDelRow(i);
						E.cy = up;
						row = &E.row[E.cy];
						for (k = row->rsize - 1; row->hl[k] == HL_MATCH && k > 0; k--);
						k++;
						if (k == 0)
							editorDelRow(E.cy);
						else {
							editorRowDelChars(row, row->size - 1, k);
							E.cy = up;
							editorRowAppendString(&E.row[E.cy], E.row[E.cy + 1].chars, E.row[E.cy + 1].size);
							editorDelRow(E.cy + 1);
						}
						E.cx = editorRowRxToCx(&E.row[E.cy], k);
					}
				}
				return;

				/* motion keys */
			case CTRL_ARROW_LEFT:
			case CTRL_ARROW_RIGHT:
			case ARROW_LEFT:
			case ARROW_RIGHT:
			case ARROW_UP:
			case ARROW_DOWN:
			case HOME_KEY:
			case END_KEY:
			case PAGE_UP:
			case PAGE_DOWN:
				{
					int up, down;
					if (E.cy < start_y) {
						up = E.cy;
						down = start_y;
					} else {
						up = start_y;
						down = E.cy;
					}
					for (int i = up; i <= down; i++) {
						editorUpdateSyntax(&E.row[i]);
						E.row[i].damaged = true;
					}
					editorProcessKeypress(c);
				}
				return;

			default:
				{
					int up, down;
					if (E.cy < start_y) {
						up = E.cy;
						down = start_y;
					} else {
						up = start_y;
						down = E.cy;
					}
					for (int i = up + 1; i < down; i++)
						editorDelRow(i);
					editorProcessKeypress(c);
				}
				return;
		}
	}
}

void editorProcessKeypress(int c) {
	static int quit_times = KILO_QUIT_TIMES;

	switch (c) {
		case '\r':
			editorInsertNewline();
			break;

		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING!!! File has unsaved changes. Press CTRL-Q %d more time%s to quit.", quit_times, quit_times > 1 ? "s" : "");
				quit_times--;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;
		
		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			if (E.cy < E.numrows)
				E.cx = E.row[E.cy].size;
			break;

		case CTRL_KEY('f'):
			editorFind();
			break;

		case CTRL_KEY('g'):
			editorJump();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (c == DEL_KEY)
				editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					E.cy = E.rowoff;
				} else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.screenrows - 1;
					if (E.cy > E.numrows)
						E.cy = E.numrows;
				}

				int times = E.screenrows;
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case SHIFT_ARROW_LEFT:
		case SHIFT_ARROW_RIGHT:
		case SHIFT_ARROW_UP:
		case SHIFT_ARROW_DOWN:
			editorSelect(c);
			break;

		case CTRL_ARROW_LEFT:
			{
				erow *row = &E.row[E.cy];
				if (E.cx == 0) {
					editorMoveCursor(ARROW_LEFT);
				} else {
					if (row->chars[E.cx] == ' ')
						while (row->chars[E.cx] == ' ')
							editorMoveCursor(ARROW_LEFT);
					while (row->chars[E.cx] != ' ' && E.cx > 0)
						editorMoveCursor(ARROW_LEFT);
				}
			}
			break;
		case CTRL_ARROW_RIGHT:
			{
				erow *row = &E.row[E.cy];
				if (E.cx == row->size) {
					editorMoveCursor(ARROW_RIGHT);
				} else {
					if (row->chars[E.cx] == ' ')
						while (row->chars[E.cx] == ' ')
							editorMoveCursor(ARROW_RIGHT);
					while (row->chars[E.cx] != ' ' && E.cx < row->size)
						editorMoveCursor(ARROW_RIGHT);
				}
			}
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertChar(c);
			break;
	}

	quit_times = KILO_QUIT_TIMES;
}

/* init */
void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows -= 2;

	signal(SIGWINCH, handleWindowResize);
}
int main(int argc, char *argv[]) {
	atexit(disableRawMode);
	enableRawMode();
	initEditor();
	if (argc >= 2)
		editorOpen(argv[1]);

	editorSetStatusMessage("HELP: CTRL-S = save | CTRL-Q = quit | CTRL-F = find | CTRL-G = jump");

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress(editorReadKey());
	}

	return 0;
}
