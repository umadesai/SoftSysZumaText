#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/ioctl.h>


// define the envioronment of ctrl key
#define CTRL_KEY(key) ((key) & 0x1f)

#define DRAW_TILDES(str, n_bytes) write(STDOUT_FILENO, str, (n_bytes))

#define ZUMA_VERSION "0.0.1"

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
} conf;

void die(const char *s) {
  editorClearScreen();
  // prints error message and exits the program
  perror(s);
  exit(1);
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

// window resize
int getWindowSize(int *n_row, int *n_col) {
  struct winsize ws;
  int flag = 0;
  if ((flag = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) != 0) {
    // TODO: Unsupported OS
    return flag;
  } else if (ws.ws_col == 0) {
    // TODO: System panic
    return -1;
  } else {
    *n_col = ws.ws_col;
    *n_row = ws.ws_row;
    return 0;
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
      switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      conf.cx--;
      break;
    case ARROW_RIGHT:
      conf.cx++;
      break;
    case ARROW_UP:
      conf.cy--;
      break;
    case ARROW_DOWN:
      conf.cy++;
      break;
  }
}

// prompt for signal processing
int editorProcessKeyPress(){
  int c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
      editorClearScreen();
      return -1;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
  return 0;
}

void initEditor() {
  conf.cx = 0;
  conf.cy = 0;
  if (getWindowSize(&conf.screenrows, &conf.screencols) == -1) die("getWindowSize");
}

void editorClearScreen(){
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < conf.screenrows; y++) {
    if (y == conf.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
        "Kilo editor -- version %s", ZUMA_VERSION);
      if (welcomelen > conf.screencols) welcomelen = conf.screencols;
      int padding = (conf.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }
    // clear lines one at a time
    abAppend(ab, "\x1b[K", 3);
    if (y < conf.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen(){
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // move the cursor to .cx,.cy position
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", conf.cy + 1, conf.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[H", 3);
  // hide the cursor when repainting
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

int main(){
  enableRawMode();
  initEditor();
  int response;
  do {
    editorRefreshScreen();
    response = editorProcessKeyPress();
  } while (!response);

  return 0;
}
