/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 31)
#define BUF_INIT                                                               \
  { NULL, 0 }
#define ROW_INIT                                                               \
  { 0, NULL }

/*** members ***/

typedef struct grow {
  int size;
  char *chars;
} grow;

struct display {
  int cx, cy;
  int rows;
  int cols;
  int cnt;
  int numRows;
  grow *row;
  char *filename;
  char statusmsg[80];
  time_t status_time;
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

int validateLine(const char *s) {
  int len = strlen(s);
  for (int i = 0; i < len; i++) {
    if (s[i] != ' ' && s[i] != '#' && s[i] != '\r' && s[i] != '\n')
      return 0;
  }
  return 1;
}

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

/*** file i/o ***/

void appendRow(char *s, size_t len) {
  d.row = realloc(d.row, sizeof(grow) * (d.numRows + 1));
  int i = d.numRows;
  d.row[i].size = len;
  d.row[i].chars = malloc(len + 1);
  memcpy(d.row[i].chars, s, len);
  d.row[i].chars[len] = '\0';
  d.numRows++;
}

void appendRows(int numrows) {
  while (numrows) {
    appendRow(NULL, 0);
    numrows--;
  }
}

// Toggle the char in position [at] of the given row
void toggleChar(grow *row, int at) {
  if (at >= row->size) {
    row->chars = realloc(row->chars, at + 2);
    row->chars[at + 1] = '\0';
    for (int i = row->size; i <= at; i++) {
      row->chars[i] = ' ';
    }
    row->size = at + 1;
  }
  row->chars[at] = row->chars[at] == '#' ? ' ' : '#';
}

// Function to toggle the character at the cursor position between ' ' and '#'
void toggleCharAtCursor() {
  if (d.cy >= d.numRows)
    appendRows(d.cy - d.numRows + 1);
  toggleChar(&d.row[d.cy], d.cx);
  if (d.cx <= d.cols - 2)
    d.cx++;
}

void openFile(char *filename) {
  free(d.filename);
  d.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    if (!validateLine(line))
      die("Invalid file format for this game.");
    appendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

void initDisplay() {
  d.cx = 0;
  d.cy = 0;
  d.cnt = 0;
  d.numRows = 0;
  d.row = NULL;
  d.filename = NULL;
  d.statusmsg[0] = '\0';
  d.status_time = 0;
  if (getDisplaySize(&d.rows, &d.cols) == -1)
    die("getDisplaySize");
  d.rows -= 2;
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

void drawRows(struct buffer *buff) {
  for (int i = 0; i < d.rows; i++) {
    if (i >= d.numRows) {
      if (d.numRows == 0 && i == d.rows / 3) {
        char helloThere[80];
        int len = snprintf(helloThere, sizeof(helloThere), "Game of Life");
        if (len > d.cols)
          len = d.cols;
        int padding = (d.cols - len) / 2;
        while (padding--)
          append(buff, " ", 1);
        append(buff, helloThere, len);
      }
    } else {
      int len = d.row[i].size;
      if (len > d.cols)
        len = d.cols;
      append(buff, d.row[i].chars, len);
    }
    append(buff, "\x1b[K", 3);
    append(buff, "\r\n", 2);
  }
}

void drawStatusBar(struct buffer *b) {
  append(b, "\x1b[7m", 4);
  char status[80];
  char rightstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
                     d.filename ? d.filename : "[No File Open]", d.numRows);
  int rlen =
      snprintf(rightstatus, sizeof(rightstatus), "%d/%d", d.cy + 1, d.cx + 1);
  if (len > d.cols)
    len = d.cols;
  append(b, status, len);
  while (len < d.cols) {
    if (d.cols - len == rlen) {
      append(b, rightstatus, rlen);
      break;
    } else {
      append(b, " ", 1);
      len++;
    }
  }

  append(b, "\x1b[m", 3);
  append(b, "\r\n", 2);
}

void drawMessageBar(struct buffer *b) {
  append(b, "\x1b[K", 3);
  int msglen = strlen(d.statusmsg);
  if (msglen > d.cols)
    msglen = d.cols;
  if (msglen && time(NULL) - d.status_time < 5)
    append(b, d.statusmsg, msglen);
}

void refreshDisplay() {
  struct buffer buff = BUF_INIT;
  append(&buff, "\x1b[?25l", 6);
  append(&buff, "\x1b[H", 3);

  drawRows(&buff);
  drawStatusBar(&buff);
  drawMessageBar(&buff);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", d.cy + 1, d.cx + 1);
  append(&buff, buf, strlen(buf));

  append(&buff, "\x1b[?25h", 6);

  write(STDOUT_FILENO, buff.b, buff.len);
  freeBuff(&buff);
}

void setStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d.statusmsg, sizeof(d.statusmsg), fmt, ap);
  va_end(ap);
  d.status_time = time(NULL);
}

/*** input methods ***/

char readKeyboardInput() {
  char c = '\0';
  if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
    die("read");
  /*if (iscntrl(c)) {
    printf("%d\r\n", c);
  } else {
    printf("%d ('%c')\r\n", c, c);
  }*/
  return c;
}

void moveCursor(char motion, int cnt) {
  if (cnt == 0)
    cnt++;
  while (cnt--) {
    switch (motion) {
    case 'h':
      if (d.cx != 0)
        d.cx--;
      break;
    case 'j':
      if (d.cy <= d.rows - 2)
        d.cy++;
      break;
    case 'k':
      if (d.cy != 0)
        d.cy--;
      break;
    case 'l':
      if (d.cx <= d.cols - 2)
        d.cx++;
      break;
    }
  }
}

void processKeypress() {
  char c = readKeyboardInput();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case 'h':
  case 'j':
  case 'k':
  case 'l':
    moveCursor(c, d.cnt);
    d.cnt = 0;
    break;
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case '0':
    if (d.cnt < 100)
      d.cnt = d.cnt * 10 + (c - 48);
    break;
  case 't':
    toggleCharAtCursor();
  }
}

/*** main ***/
int main(int argc, char *argv[]) {
  enableRawMode();
  initDisplay();
  if (argc >= 2) {
    openFile(argv[1]);
  }
  //  openFile("testfile");
  setStatusMessage("HELP: Ctrl-q = quit");
  while (1) {
    refreshDisplay();
    processKeypress();
  }
  return 0;
}
