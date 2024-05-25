/*** includes ***/

#include <bits/types/struct_iovec.h>
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#define DFLT_MSG "HELP: Ctrl-Q = quit | Ctrl-S = save"

/*** enums ***/

enum key {
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

/*** prototypes ***/

void setStatusMessage(const char *fmt, ...);
void refreshDisplay();
int readKeyboardInput();
char *promptUser(char *prompt);

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
  int tempnumRows;
  grow *row;
  grow *temprow;
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

// Convert array of grow structs to a single string buffer to be saved to disk
char *rowsToString(int *len) {
  int totlen = 0;
  for (int i = 0; i < d.numRows; i++) {
    totlen += d.row[i].size + 1;
  }
  *len = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (int i = 0; i < d.numRows; i++) {
    memcpy(p, d.row[i].chars, d.row[i].size);
    p += d.row[i].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void appendRow(char *s, size_t len) {
  d.row = realloc(d.row, sizeof(grow) * (d.numRows + 1));
  int i = d.numRows;
  d.row[i].size = len;
  d.row[i].chars = malloc(len + 1);
  memcpy(d.row[i].chars, s, len);
  d.row[i].chars[len] = '\0';
  d.numRows++;
}

void appendRowTemp(char *s, size_t len) {
  d.temprow = realloc(d.temprow, sizeof(grow) * (d.tempnumRows + 1));
  int i = d.tempnumRows;
  d.temprow[i].size = len;
  d.temprow[i].chars = malloc(len + 1);
  memcpy(d.temprow[i].chars, s, len);
  d.temprow[i].chars[len] = '\0';
  d.tempnumRows++;
}

void copyToTemp() {
  for (int i = 0; i < d.numRows; i++) {
    if (i > d.tempnumRows) {
      appendRowTemp(d.row[i].chars, d.row[i].size);
    } else {
      d.temprow[i].chars = realloc(d.temprow[i].chars, d.row[i].size);
      memcpy(d.temprow[i].chars, d.row[i].chars, d.row[i].size);
    }
  }
}

void appendRows(int numrows) {
  char *s = malloc(d.cols + 1);
  for (int i = 0; i < d.cols; i++) {
    s[i] = ' ';
  }
  s[d.cols] = '\0';
  while (numrows) {
    appendRow(s, d.cols);
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
  if (d.rows - 1 >= d.numRows)
    appendRows(d.rows - 1 - d.numRows + 1);
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

void saveFile() {
  if (d.filename == NULL) {
    d.filename = promptUser("Save as: %s (ESC to cancel)");
    if (d.filename == NULL) {
      setStatusMessage("Save aborted.");
      return;
    }
  }
  int len;

  char *buf = rowsToString(&len);

  int fd = open(d.filename, O_RDWR | O_CREAT, 0644);
  if (fd != 1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        setStatusMessage("%d bytes written to disk. Filename %s", len,
                         d.filename);
        return;
      }
    }
    close(fd);
  }
  free(buf);
}

void initDisplay() {
  d.cx = 0;
  d.cy = 0;
  d.cnt = 0;
  d.numRows = 0;
  d.tempnumRows = 0;
  d.row = NULL;
  d.temprow = NULL;
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

void welcomeMsg(struct buffer *buff, const char *msg) {
  int len = strlen(msg);
  if (len > d.cols)
    len = d.cols;
  int padding = (d.cols - len) / 2;
  while (padding--)
    append(buff, " ", 1);
  append(buff, msg, len);
}

void drawRows(struct buffer *buff) {
  for (int i = 0; i < d.rows; i++) {
    if (i >= d.numRows) {
      int start = d.rows / 3;
      if (d.numRows == 0 && i == start) {
        welcomeMsg(buff, "Game Of Life");
      } else if (d.numRows == 0 && i == start + 2) {
        welcomeMsg(buff, "Move the Cursor with 'h','j','k','l'");
      } else if (d.numRows == 0 && i == start + 4) {
        welcomeMsg(buff, "Toggle the character at the cursor with 't'");
      } else if (d.numRows == 0 && i == start + 6) {
        welcomeMsg(buff, "Press [ENTER] to start a cycle");
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

  setStatusMessage(DFLT_MSG);
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

char *promptUser(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t len = 0;
  buf[0] = '\0';

  while (1) {
    setStatusMessage(prompt, buf);
    refreshDisplay();

    int c = readKeyboardInput();
    if (c == DEL_KEY || c == BACKSPACE) {
      if (len != 0)
        buf[--len] = '\0';
    } else if (c == '\x1b') {
      setStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (len != 0) {
        setStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (len == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[len++] = c;
      buf[len] = '\0';
    }
  }
}

int readKeyboardInput() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    char seq[3];
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
  } else {
    return c;
  }
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

  case CTRL_KEY('s'):
    saveFile();
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

/*** game logic ***/

void executeGeneration() {
  copyToTemp();
  for (int i = 0; i < d.numRows; i++) {
    for (int j = 0; j < d.cols; j++) {
      int liveNeighbors = 0;
      if (i > 0) {
        // check above
      }
      if (i < d.numRows) {
        // check below
      }
      if (j > 0) {
        // check left
      }
      if (j < d.cols) {
        // check right
      }
    }
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
  setStatusMessage(DFLT_MSG);
  while (1) {
    refreshDisplay();
    processKeypress();
  }
  return 0;
}
