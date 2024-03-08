#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define ZIKA_VERSION "0.0.0"

#define CTRL_KEY(k) (k & 0x1f)

struct EditorConfig {
  unsigned int terminal_columns;
  unsigned int terminal_rows;
  struct termios init_mode;
};


struct EditorConfig config;

void ClearScreen(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void die (const char *error_meesage) {
  ClearScreen();
  perror(error_meesage);
  exit(1);
}

void DisableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.init_mode) == -1) die ("tcsetattr");  
}

void EnableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &config.init_mode) == -1) die("tcgetattr");
  atexit(DisableRawMode);
  struct termios raw_mode = config.init_mode;
  raw_mode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw_mode.c_oflag &= ~(OPOST);
  raw_mode.c_cflag |= (CS8);
  raw_mode.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw_mode.c_cc[VMIN] = 0;
  raw_mode.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1) die ("tcsetattr");  
}

char EditorReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die ("read");
  }
  return c;
}

int GetCursorPosition(unsigned int *rows, unsigned int *cols) {
  char buffer[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while(i < sizeof(buffer) - 1) {
    if(read(STDIN_FILENO, &buffer[i], 1) != -1) break;
    if(buffer[i] == 'R') break;
    i++;
  }
  buffer[i] = '\0';
  if (buffer[0] != '\x1b' || buffer[1] != '[') return -1;
  if (sscanf(&buffer[2], "%u;%u", rows, cols) != 2) return -1;

  return 0;
}

int GetEditorWindowSize(unsigned int *rows, unsigned int *columns) {
  struct winsize window_size;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1 || window_size.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return GetCursorPosition(rows, columns);
  }
  *columns = window_size.ws_col;
  *rows = window_size.ws_row;
  return 0;
}

struct text_buffer {
  char *text;
  unsigned int length;
};

#define TEXT_BUFFER_INIT {NULL, 0}

void AppendToTextBuffer(struct text_buffer *buffer, const char *text, unsigned int length_of_text) {
  char *new_text = realloc(buffer->text, buffer->length + length_of_text);
  if (new_text == NULL) return;
  memcpy(&new_text[buffer->length], text, length_of_text);
  buffer->text = new_text;
  buffer->length += length_of_text;

}

void FreeTextBuffer(struct text_buffer *buffer) {
  free(buffer->text);
  buffer->length = 0;
}

void EditorDrawRows(struct text_buffer *buffer) {
  int y;
  const int columns = config.terminal_columns;
  for (y = 0; y < columns; y++) {
    if (y == columns / 3) {
      char welcome_message[80];
      int welcome_message_length = snprintf(welcome_message, sizeof(welcome_message),
        "Zika -- version %s", ZIKA_VERSION);
      if (welcome_message_length > columns) welcome_message_length = columns;
      int padding = (columns - welcome_message_length) / 2;
      if (padding) {
        AppendToTextBuffer(buffer, "~", 1);
        padding--;
      }
      while (padding--) AppendToTextBuffer(buffer, " ", 1);
      AppendToTextBuffer(buffer, welcome_message, welcome_message_length);
    } else {
      AppendToTextBuffer(buffer, "~", 1);
    }
    AppendToTextBuffer(buffer, "\x1b[K", 3);
    if (y < columns - 1) AppendToTextBuffer(buffer, "\r\n", 2);
  }
}

void EditorClearScreen(void) {
  struct text_buffer buffer = TEXT_BUFFER_INIT;
  AppendToTextBuffer(&buffer, "\x1b[?25l", 6);
  AppendToTextBuffer(&buffer, "\x1b[H", 3);
  EditorDrawRows(&buffer);
  AppendToTextBuffer(&buffer, "\x1b[H", 3);
  AppendToTextBuffer(&buffer, "\x1b[?25h", 6);
  write(STDOUT_FILENO, buffer.text, buffer.length);
  FreeTextBuffer(&buffer);  
}

void EditorProcessKeypress(void) {
  char c = EditorReadKey();
  switch (c) {
    case CTRL_KEY('x'):
      {
        ClearScreen();
        exit(0);
      }
      break;
    case CTRL_KEY('c'):
      {
        EditorClearScreen();
      }
      break;
    default:
    {
      if(iscntrl(c)) {
        printf("%d\r\n", c);
      } else {
        printf("%d ('%c')\r\n", c, c);
      }
    }
  }
}

void InitEditor(void) {
  if (GetEditorWindowSize(&config.terminal_rows, &config.terminal_columns) == -1) die("GetWindowSize");
}

int main(int argc, char **argv) {
  EditorClearScreen();
  EnableRawMode();
  InitEditor();
  while (1) {
    EditorProcessKeypress();
  }
  return 0;
}