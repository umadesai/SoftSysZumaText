#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/ioctl.h>


// define the envioronment of ctrl key
#define CTRL_KEY(key) ((key) & 0x1f)

#define DRAW_TILDES(str, n_bytes) write(STDOUT_FILENO, str, (n_bytes))

struct editorConfig {
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

// prompt for key
char editorReadKey() {
  int n_read;
  char c;
  while ((n_read = read(STDIN_FILENO, &c, 1)) != 1)
    if (n_read == -1 && errno != EAGAIN) die("editorReadKey");
  return c;
}

// prompt for signal processing
int editorProcessKeyPress(){
  char c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
      editorClearScreen();
      return -1;
  }
  return 0;
}

void initEditor() {
  if (getWindowSize(&conf.screenrows, &conf.screencols) == -1) die("getWindowSize");
}

void editorClearScreen(){
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorRefreshScreen(){
  editorClearScreen();
  int y;
  for (y = 0; y < conf.screenrows; y++) {
    DRAW_TILDES("~", 1);
    if (y < conf.screenrows - 1) {
      DRAW_TILDES("\r\n", 2);
    }
  }
  write(STDOUT_FILENO, "\x1b[H", 3);
}

int main(){
  enableRawMode();
  initEditor();
  do {
    editorRefreshScreen();
    response = editorProcessKeypress();
  } while (!response);

  return 0;
}
