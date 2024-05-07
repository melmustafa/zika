#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define ZIKA_VERSION "0.1.0"

#define CTRL_KEY(k) (k & 0x1f)

enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DELETE_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

struct EditorConfig {
  unsigned int cursor_x, cursor_y;
  unsigned int terminal_columns;
  unsigned int terminal_rows;
  termios init_mode;
};


EditorConfig config;

void ClearScreen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void die (const char *error_message) {
  ClearScreen();
  perror(error_message);
  exit(1);
}

void DisableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.init_mode) == -1) die ("tcsetattr");  
}

void EnableRawMode() {
  if (tcgetattr(STDIN_FILENO, &config.init_mode) == -1) die("tcgetattr");
  atexit(DisableRawMode);
  termios raw_mode = config.init_mode;
  raw_mode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw_mode.c_oflag &= ~(OPOST);
  raw_mode.c_cflag |= (CS8);
  raw_mode.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw_mode.c_cc[VMIN] = 0;
  raw_mode.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1) die ("tcsetattr");  
}

int EditorReadKey() {
  int number_of_read;
  char pressed_key;
  while ((number_of_read = read(STDIN_FILENO, &pressed_key, 1)) != 1) {
    if (number_of_read == -1 && errno != EAGAIN) die ("read");
  }
  if (pressed_key != '\x1b') return pressed_key;
  char sequence[3];
  if (read(STDIN_FILENO, &sequence[0], 1) != 1) return '\x1b';
  if (read(STDIN_FILENO, &sequence[1], 1) != 1) return '\x1b';
  if (sequence[1] >= '0' && sequence[1] <= '9') {
    if (read(STDIN_FILENO, &sequence[2], 1) != 1 || sequence[2] != '~') return '\x1b';
    switch (sequence[1]) {
    case '1': return HOME_KEY;    
    case '3': return DELETE_KEY;
    case '4': return END_KEY;
    case '5': return PAGE_UP;
    case '6': return PAGE_UP;
    case '7': return HOME_KEY;
    case '8': return END_KEY;
    }
  }
  if (sequence[0] != '[' && sequence[0] != 'O') return '\x1b';
  switch (sequence[1]) {
    case 'A': return ARROW_UP;
    case 'B': return ARROW_DOWN;
    case 'C': return ARROW_RIGHT;
    case 'D': return ARROW_LEFT;
    case 'H': return HOME_KEY;
    case 'F': return END_KEY;
  }
  return pressed_key;
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
  winsize window_size;
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

void AppendToTextBuffer(text_buffer *buffer, const char *text, unsigned int length_of_text) {
  char *new_text = (char*)realloc(buffer->text, buffer->length + length_of_text);
  if (new_text == NULL) return;
  memcpy(&new_text[buffer->length], text, length_of_text);
  buffer->text = new_text;
  buffer->length += length_of_text;

}

void FreeTextBuffer(text_buffer *buffer) {
  free(buffer->text);
  buffer->length = 0;
}

void EditorDrawRows(text_buffer *buffer) {
  int y;
  const int rows = config.terminal_rows;
  const int columns = config.terminal_columns;
  for (y = 0; y < rows; y++) {
    if (y==0) continue;
    if (y == 1) {
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
    if (y < rows - 1) AppendToTextBuffer(buffer, "\r\n", 2);
  }
}

void EditorClearScreen() {
  text_buffer buffer = TEXT_BUFFER_INIT;
  AppendToTextBuffer(&buffer, "\x1b[?25l", 6);
  AppendToTextBuffer(&buffer, "\x1b[H", 3);
  EditorDrawRows(&buffer);
  char cursor_buffer[32];
  snprintf(cursor_buffer, sizeof(cursor_buffer), "\x1b[%d;%dH", config.cursor_y + 1, config.cursor_x + 1);
  AppendToTextBuffer(&buffer, cursor_buffer, strlen(cursor_buffer));
  AppendToTextBuffer(&buffer, "\x1b[?25h", 6);
  write(STDOUT_FILENO, buffer.text, buffer.length);
  FreeTextBuffer(&buffer);  
}

void MoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if (config.cursor_x > 0) {
        config.cursor_x--;
      }
      break;
    case ARROW_RIGHT:
      if (config.cursor_x < config.terminal_columns) {
        config.cursor_x++;
      }
      break;
    case ARROW_UP:
      if (config.cursor_y > 0) {
        config.cursor_y--;
      }
      break;
    case ARROW_DOWN:
      if (config.cursor_y < config.terminal_rows) {
        config.cursor_y++;
      }
      break;
    // case PAGE_UP:
    // case PAGE_DOWN:
    //   {
    //     config.cursor_y =+ (key == PAGE_DOWN? 1:(-1)) * config.terminal_rows;
    //   }
    //   break;
  }
}

void EditorProcessKeypress() {
  int pressed_key = EditorReadKey();
  switch (pressed_key) {
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
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_RIGHT:
    case ARROW_LEFT:
      {
        MoveCursor(pressed_key);
      }
      break;
    case PAGE_UP:
      {
        config.cursor_y = 0;
      }
      break;
    case PAGE_DOWN:
      // {
      //   unsigned int rows = config.terminal_rows;
      //   int key = (pressed_key == PAGE_UP)? ARROW_UP: ARROW_DOWN;
      //   while(rows--) {
      //     MoveCursor(key);
      //   }
      // }
      {
        config.cursor_y = config.terminal_rows - 1;
      }
      break;
    case HOME_KEY:
      {
        config.cursor_x = 0;
      }
      break;
    case END_KEY:
      // {
      //   unsigned int columns = config.terminal_columns;
      //   int key = (pressed_key == HOME_KEY)? ARROW_LEFT: ARROW_RIGHT;
      //   while(columns--) {
      //     MoveCursor(key);
      //   }
      // }
      {
        config.cursor_x = config.terminal_columns - 1;
      }
      break;
    default:
    {
      if(iscntrl(pressed_key)) {
        printf("%d\r\n", pressed_key);
      } else {
        printf("%d ('%c')\r\n", pressed_key, pressed_key);
      }
    }
  }
  printf("Current cursor postion: (%d, %d)", config.cursor_x, config.cursor_y);
}

void InitEditor() {
  config.cursor_x = 0;
  config.cursor_y = 0;
  if (GetEditorWindowSize(&config.terminal_rows, &config.terminal_columns) == -1) die("GetWindowSize");
}

int main(int argc, char **argv) {
  EnableRawMode();
  InitEditor();
  EditorClearScreen();
  while (1) {
    EditorProcessKeypress();
  }
  return 0;
}
