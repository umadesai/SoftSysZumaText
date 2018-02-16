#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios orig_termios;

// turn off echoing
void enableRawMode(){
  struct termios raw;

  tcgetattr(STDIN_FILENO, &raw);

  raw.c_lflag &= ~(ECHO);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// disable raw mode at exit
void disableRawMode(){
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disableRawMode);

  struct termious raw = orig_termios;
  raw.c_lflag &= ~(ECHO);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int main(){
  enableRawMode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
  return 0;
}
