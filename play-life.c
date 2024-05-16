/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 31)

/*** members ***/

struct termios orig;

/*** methods ***/

void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig;
  raw.c_iflag &= ~(ICRNL | IXON | ISTRIP | INPCK | BRKINT);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag &= ~(CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  // raw.c_cc[VMIN] = 0;
  //  raw.c_cc[VTIME] = 10;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

/*** output methods ***/

void refreshDisplay() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input methods ***/

char readKeyboardInput() {
  char c = '\0';
  if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
    die("read");
  if (iscntrl(c)) {
    printf("%d\r\n", c);
  } else {
    printf("%d ('%c')\r\n", c, c);
  }
  return c;
}

void processKeypress() {
  char c = readKeyboardInput();

  if (c == CTRL_KEY('q'))
    exit(0);
}

/*** main ***/
int main() {
  enableRawMode();

  while (1) {
    refreshDisplay();
    processKeypress();
  }

  return 0;
}
