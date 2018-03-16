#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdarg.h>
#include "zuma.h"


// define the envioronment of ctrl key
#define CTRL_KEY(key) ((key) & 0x1f)

#define DRAW_TILDES(str, n_bytes) write(STDOUT_FILENO, str, (n_bytes))

#define ZUMA_VERSION "0.0.1"
#define ZUMA_TAB_STOP 4
#define KILO_QUIT_TIMES 2

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_NUMBER
};


#define HL_HIGHLIGHT_NUMBERS (1<<0)

struct editorRow {
  int size;
  int rsize;
  char* chars;
  char* render;
  unsigned char *hl;
};

struct editorSyntax {
  char *filetype;
  char **filematch;
  int flags;
};

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff, coloff;
  int screenrows, screencols;
  int nrows;
  struct editorRow* row;
  char *filename;
  int dirty;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  struct termios orig_termios;
} conf;



/*** filetypes ***/
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    HL_HIGHLIGHT_NUMBERS
  },
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

void editorClearScreen(){
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}


void die(const char *s) {
  editorClearScreen();
  // prints error message and exits the program
  perror(s);
  exit(1);
}

int editorRowCxToRx(struct editorRow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (ZUMA_TAB_STOP - 1) - (rx % ZUMA_TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(struct editorRow *row, int rx) {
  int crx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      crx += (ZUMA_TAB_STOP - 1) - (crx % ZUMA_TAB_STOP);
    crx++;
    if (crx > rx) return cx;
  }
  return cx;
}


void editorUpdateRow(struct editorRow *row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++) if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs * (ZUMA_TAB_STOP - 1) + 1);
  int index = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[index++] = ' ';
      while (index % ZUMA_TAB_STOP != 0) row->render[index++] = ' ';
    } else {
      row->render[index++] = row->chars[j];
    }
  }
  row->render[index] = '\0';
  row->rsize = index;


  editorUpdateSyntax(row);
}


void editorInsertRow(int loc, char *line, size_t linelen) {
  if (loc < 0 || loc > conf.nrows) return;

  conf.row = realloc(conf.row, sizeof(struct editorRow) * (conf.nrows + 1));
  memmove(&conf.row[loc + 1], &conf.row[loc], sizeof(struct editorRow) * (conf.nrows - loc));

  conf.row[loc].size = linelen;
  conf.row[loc].chars = malloc(linelen + 1);
  memcpy(conf.row[loc].chars, line, linelen);
  conf.row[loc].chars[linelen] = '\0';

  conf.row[loc].rsize = 0;
  conf.row[loc].render = NULL;
  conf.row[loc].hl = NULL;
  editorUpdateRow(&conf.row[loc]);
  conf.nrows++; conf.dirty++;

}

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < conf.nrows; j++)
    totlen += conf.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < conf.nrows; j++) {
    memcpy(p, conf.row[j].chars, conf.row[j].size);
    p += conf.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(conf.statusmsg, sizeof(conf.statusmsg), fmt, ap);
  va_end(ap);
  conf.statusmsg_time = time(NULL);
}


void editorOpen(char* filename) {

  free(conf.filename);
  conf.filename = strdup(filename);

  editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  // read from the file
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))  linelen--;
    editorInsertRow(conf.nrows, line, linelen);

  }
  free(line);
  fclose(fp);
  conf.dirty = 0;
}

void editorSave() {
  if (!conf.filename) {
    conf.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (!conf.filename) return;
  }
  editorSelectSyntaxHighlight();

  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(conf.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        conf.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("I/O error: %s", strerror(errno));
}


void editorFindCallback(char *query, int key) {
  if (key == '\r' || key == '\x1b') {
    return;
  }
  int i;
  for (i = 0; i < conf.nrows; i++) {
    struct editorRow *row = &conf.row[i];
    char *match = strstr(row->render, query);
    if (match) {
      conf.cy = i;
      conf.cx = editorRowRxToCx(row, match - row->render);
      conf.rowoff = conf.nrows;
      break;
    }
  }
}

void editorFind() {
  char *query = editorPrompt("Search: %s (ESC to cancel)", editorFindCallback);
  if (!query) return;
  free(query);
}

// disable raw mode at exit
void disableRawMode(){
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &(conf.orig_termios)) == -1)
    die("tcsetattr");
}

// turn off echoing and canonical mode
void enableRawMode(){
  if (tcgetattr(STDIN_FILENO, &(conf.orig_termios)) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = (conf.orig_termios);

  // fix Ctrl-M and disable Ctrl-S, Ctrl-Q, and other miscellaneous flags
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // turn off all output processing
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  // disable Ctrl-C, Ctrl-Z, and Ctrl-V
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int getCursorPosition(int *n_row, int *n_col) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", n_row, n_col) != 2) return -1;
  return 0;
}

// window resize
int getWindowSize(int *n_row, int *n_col) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(n_row, n_col);
    // editorReadKey();
    // return -1;
  } else {
    *n_col = ws.ws_col;
    *n_row = ws.ws_row;
    return 0;
  }
}


/*** syntax highlighting ***/
void editorUpdateSyntax(struct editorRow* row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (conf.syntax == NULL) return;


  int i;
  for (i = 0; i < row->rsize; i++) {
    if (isdigit(row->render[i])) {
      row->hl[i] = HL_NUMBER;
    }
  }
}

int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_NUMBER: return 31;
    default: return 37;
  }
}

void editorSelectSyntaxHighlight() {
  conf.syntax = NULL;
  if (conf.filename == NULL) return;
  char *ext = strrchr(conf.filename, '.');
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(conf.filename, s->filematch[i]))) {
        conf.syntax = s;

        int filerow;
        for (filerow = 0; filerow < conf.nrows; filerow++) {
          editorUpdateSyntax(&conf.row[filerow]);
        }
        return;
      }
      i++;
    }
  }
}

// append buffer
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

// prompt for key
int editorReadKey() {
  int n_read;
  char c;
  while ((n_read = read(STDIN_FILENO, &c, 1)) != 1){
    if (n_read == -1 && errno != EAGAIN) die("editorReadKey");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY; // esc [1~
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP; // esc [A
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;  // esc 0H
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

void editorMoveCursor(int key) {
  struct editorRow *row = (conf.cy >= conf.nrows) ? NULL : &conf.row[conf.cy];
  switch (key) {
    case ARROW_LEFT:
      if (conf.cx != 0) conf.cx--;  else if (conf.cy > 0) {
        conf.cy--;
        conf.cx = conf.row[conf.cy].size;
      }
      break;
    case ARROW_RIGHT:
      // limit scrolling
      if (row && conf.cx < row->size) conf.cx++; else if (row && conf.cx == row->size) {
        conf.cy++;
        conf.cx = 0;
      }
      break;
    case ARROW_UP:
      if (conf.cy != 0) conf.cy--;
      break;
    case ARROW_DOWN:
      if (conf.cy < conf.nrows) conf.cy++;
      break;
  }
  row = (conf.cy >= conf.nrows) ? NULL : &conf.row[conf.cy];
  int rowlen = row ? row->size : 0;
  if (conf.cx > rowlen) conf.cx = rowlen;
}


void editorInsertNewline() {
  if (conf.cx == 0) {
    editorInsertRow(conf.cy, "", 0);
  } else {
    struct editorRow *row = &conf.row[conf.cy];
    editorInsertRow(conf.cy + 1, &row->chars[conf.cx], row->size - conf.cx);
    row = &conf.row[conf.cy];
    row->size = conf.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  conf.cy++;
  conf.cx = 0;
}


void editorRowInsertChar(struct editorRow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  conf.dirty++;
}

void editorRowDelChar(struct editorRow *row, int loc) {
  if (loc < 0 || loc >= row->size) return;
  memmove(&row->chars[loc], &row->chars[loc + 1], row->size - loc);
  row->size--;
  editorUpdateRow(row);
  conf.dirty++;
}

void editorFreeRow(struct editorRow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}
void editorDelRow(int loc) {
  if (loc < 0 || loc >= conf.nrows) return;
  editorFreeRow(&conf.row[loc]);
  memmove(&conf.row[loc], &conf.row[loc + 1],
    sizeof(struct editorRow) * (conf.nrows - loc - 1));
  conf.nrows--;
  conf.dirty++;
}

void editorRowAppendString(struct editorRow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  conf.dirty++;
}

void editorDelChar() {
  if (conf.cy == conf.nrows) return;
  if (conf.cx == 0 && conf.cy == 0) return;
  struct editorRow *row = &conf.row[conf.cy];
  if (conf.cx > 0) {
    editorRowDelChar(row, conf.cx - 1);
    conf.cx--;
  } else {
    conf.cx = conf.row[conf.cy - 1].size;
    editorRowAppendString(&conf.row[conf.cy - 1], row->chars, row->size);
    editorDelRow(conf.cy);
    conf.cy--;
  }
}

void editorInsertChar(int c) {
  if (conf.cy == conf.nrows) {
    editorInsertRow(conf.nrows,"", 0);
  }
  editorRowInsertChar(&conf.row[conf.cy], conf.cx, c);
  conf.cx++;
}




// prompt for signal processing
int editorProcessKeyPress(){
  static int quit_times = KILO_QUIT_TIMES;
  int c = editorReadKey();
  switch (c) {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
      if (conf.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                               "Press Ctrl-Q %d more time to quit.",
                               quit_times);
        quit_times--;
        return 0;
      }
      editorClearScreen();
      return -1;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      conf.cx = 0;
      break;
    case END_KEY:
      if (conf.cy < conf.nrows) conf.cx = conf.row[conf.cy].size;
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;


    case CTRL_KEY('l'):
    case '\x1b':
      break;


    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
      if (c == PAGE_UP) {
        conf.cy = conf.rowoff;
      } else if (c == PAGE_DOWN) {
        conf.cy = conf.rowoff + conf.screenrows - 1;
        if (conf.cy > conf.nrows) conf.cy = conf.nrows;
      }
      int times = conf.screenrows;
      while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
      break;

    default:
      editorInsertChar(c);
      break;

  }
  quit_times = KILO_QUIT_TIMES;
  return 0;
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(conf.statusmsg);
  if (msglen > conf.screencols) msglen = conf.screencols;
  if (msglen && time(NULL) - conf.statusmsg_time < 5)
    abAppend(ab, conf.statusmsg, msglen);
}



//std arg

void initEditor() {
  conf.rx = conf.cx = conf.cy = 0;
  conf.nrows = 0;
  conf.rowoff = conf.coloff = 0;
  conf.row = NULL;
  conf.dirty = 0;
  conf.filename = NULL;
  conf.statusmsg[0] = '\0';
  conf.statusmsg_time = 0;
  conf.syntax = NULL;

  if (getWindowSize(&conf.screenrows, &conf.screencols) == -1)
    die("getWindowSize");
  conf.screenrows -= 2;
}



void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < conf.screenrows; y++) {
    int filerow = y + conf.rowoff;
    if (y + conf.rowoff >= conf.nrows) {
      if (y == conf.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "ZUMA editor -- version %s", ZUMA_VERSION);
        if (welcomelen > conf.screencols) welcomelen = conf.screencols;
        int padding = (conf.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        // abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      // adjustment
      int len = conf.row[y+conf.rowoff].rsize - conf.coloff;
      len = (len < 0) ? 0 : len;
      if (len > conf.screencols) len = conf.screencols;

      char *c = &conf.row[filerow].render[conf.coloff];
      unsigned char *hl = &conf.row[filerow].hl[conf.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
            abAppend(ab, &c[j], 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }
    // clear lines one at a time
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorVScroll() {
  conf.rx = 0;
  if (conf.cy < conf.nrows) {
    conf.rx = editorRowCxToRx(&conf.row[conf.cy], conf.cx);
  }
  if (conf.cy < conf.rowoff) {
    conf.rowoff = conf.cy;
  }
  if (conf.cy >= conf.rowoff + conf.screenrows) {
    conf.rowoff = conf.cy - conf.screenrows + 1;
  }
}

void editorHScroll(){
  if (conf.cx < conf.coloff) {
    conf.coloff = conf.rx;
  }
  if (conf.cx >= conf.coloff + conf.screencols) {
    conf.coloff = conf.rx - conf.screencols + 1;
  }
}



void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    conf.filename ? conf.filename : "[New File]",
    conf.nrows, conf.dirty ? "*" : "");

  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    conf.syntax ? conf.syntax->filetype : "no ft", conf.cy + 1, conf.nrows);

  if (len > conf.screencols) len = conf.screencols;
  abAppend(ab, status, len);
  while (len < conf.screencols) {
    if (conf.screencols - len == rlen) {
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

void editorRefreshScreen(){
  editorVScroll();
  editorHScroll();
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // move the cursor to .cx,.cy position
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
           conf.cy + 1 - conf.rowoff,
           conf.rx + 1 - conf.coloff);
  abAppend(&ab, buf, strlen(buf));

  // hide the cursor when repainting
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

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
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
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
    if (callback) callback(buf, c);
  }
}

int main(int argc, char** argv)
{
  enableRawMode();
  initEditor();
  if (argc >= 2) editorOpen(argv[1]);
  int response;
  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  do {
    editorRefreshScreen();
    response = editorProcessKeyPress();
  } while (!response);

  return 0;
}
