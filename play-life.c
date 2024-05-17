/*** includes ***/
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k)&31)
#define BUF_INIT                                                               \
  { NULL, 0 }

/*** members ***/

struct display {
  int rows;
  int cols;
  struct termios orig;
};
struct display d;

/*** dynamic string ***/

struct buffer {
  char *b;
  int len;
};

void append(struct buffer *buf, const char *s, int len) {
  char *new = realloc(buf->b, buf->len + len);
  if (new == NULL)
    return;
  memcpy(&new[buf->len], s, len);
  buf->b = new;
  buf->len += len;
}

void prepend(struct buffer *buf, char *s, int len) {
  char *new = realloc(s, buf->len + len);
  if (new == NULL)
    return;
  memcpy(&new[len], buf->b, buf->len);
  buf->b = new;
  buf->len += len;
}

void freeBuff(struct buffer *buff) { free(buff->b); }

/*** methods ***/

void itoa(int i, struct buffer *buff) {
  while (i > 0) {
    char *str = malloc(sizeof(char));
    *str = (char)((i % 10) + 48);
    prepend(buff, str, 1);
    i /= 10;
  }
}

int getDisplaySize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void initDisplay() {
  if (getDisplaySize(&d.rows, &d.cols) == -1)
    die("getDisplaySize");
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &d.orig) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &d.orig) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = d.orig;
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

void drawTildes(struct buffer *buff) {
  /*
  append(buff, " ", 1);
  for (int i = 1; i < d.cols; i++) {
    int *len = malloc(sizeof(int));
    char *s = itoa(i, len);
    append(buff, s, *len);
  }
  append(buff, "\r\n", 2);
  for (int i = 1; i < d.rows; i++) {
    int *len = malloc(sizeof(int));
    char *s = itoa(i, len);
    append(buff, s, *len);

    if (i < d.rows - 1) {
      append(buff, "\r\n", 2);
    }
  }
  */
  char *new;
  int len = (d.rows) * (d.cols + 2);
  new = malloc(sizeof(char) * len);
  for (int i = 1; i <= d.rows; i++) {
    for (int j = 1; j <= d.cols; j++) {
      if (i == 3 && j > 3) {
        struct buffer num = BUF_INIT;
        itoa(j - 3, &num);
        char t = num.b[0];
        if (num.len == 3) {
          new[(i - 1) * (d.cols + 2) + j - 1] = num.b[2];
          new[(i - 2) * (d.cols + 2) + j - 1] = num.b[1];
          new[(i - 3) * (d.cols + 2) + j - 1] = num.b[0];
        } else if (num.len == 2) {
          new[(i - 1) * (d.cols + 2) + j - 1] = num.b[1];
          new[(i - 2) * (d.cols + 2) + j - 1] = num.b[0];
        } else if (num.len == 1) {
          new[(i - 1) * (d.cols + 2) + j - 1] = num.b[0];
        }
      } else if (j == 3 && i > 3) {
        struct buffer num = BUF_INIT;
        itoa(i - 3, &num);
        if (num.len == 3) {
          new[(i - 1) * (d.cols + 2) + j - 1] = num.b[2];
          new[(i - 1) * (d.cols + 2) + j - 2] = num.b[1];
          new[(i - 1) * (d.cols + 2) + j - 3] = num.b[0];
        } else if (num.len == 2) {
          new[(i - 1) * (d.cols + 2) + j - 1] = num.b[1];
          new[(i - 1) * (d.cols + 2) + j - 2] = num.b[0];
        } else if (num.len == 1) {
          new[(i - 1) * (d.cols + 2) + j - 1] = num.b[0];
        }
      } else {
        new[(i - 1) * (d.cols + 2) + j - 1] = ' ';
      }
    }
    new[(i - 1) * (d.cols + 2) + d.cols] = '\r';
    new[(i - 1) * (d.cols + 2) + d.cols + 1] = '\n';
  }
  append(buff, new, len);
}

void refreshDisplay() {
  struct buffer buff = BUF_INIT;
  append(&buff, "\x1b[2J", 4);
  append(&buff, "\x1b[H", 3);

  drawTildes(&buff);

  append(&buff, "\x1b[H", 3);

  write(STDOUT_FILENO, buff.b, buff.len);
  freeBuff(&buff);
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

  if (c == CTRL_KEY('q')) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    exit(0);
  }
}

/*** main ***/
int main() {
  enableRawMode();
  initDisplay();

  while (1) {
    refreshDisplay();
    processKeypress();
  }
  return 0;
}
