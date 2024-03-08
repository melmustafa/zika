#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios init_mode;

void die (const char *error_meesage) {
  perror(error_meesage);
  exit(1);
}

void DisableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &init_mode) == -1) die ("tcsetattr");  
}

void EnableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &init_mode) == -1) die("tcgetattr");
  atexit(DisableRawMode);
  struct termios raw_mode = init_mode;
  raw_mode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw_mode.c_oflag &= ~(OPOST);
  raw_mode.c_cflag |= (CS8);
  raw_mode.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw_mode.c_cc[VMIN] = 0;
  raw_mode.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1) die ("tcsetattr");  
}

int main(int argc, char **argv) {
  EnableRawMode();
  while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read ");
    if(iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') break;
  }
  return 0;
}