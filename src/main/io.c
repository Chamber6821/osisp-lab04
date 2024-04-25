#include "io.h"

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int getch() {
  struct termios old, current;
  tcgetattr(STDIN_FILENO, &current);
  old = current;
  current.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSANOW, &current);
  int ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  return ch;
}

void bytes2hex(char *string, int length, char bytes[]) {
  char *it = string;
  if (length) {
    sprintf(it, "%02hhX", bytes[0]);
    it += 2;
  }
  for (int i = 1; i < length; i++) {
    sprintf(it, ":%02hhX", bytes[i]);
    it += 3;
  }
}
