#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  char r[1];
  char w[1] = "x";
  int p1[2];
  int p2[2];

  pipe(p1);
  pipe(p2);

  if (fork() == 0) {
    close(p1[1]);
    close(p2[0]);

    if (read(p1[0], r, 1) != 1) {
      printf("%d: read error\r\n", getpid());
      exit(1);
    }

    printf("%d: received ping\r\n", getpid());

    if (write(p2[1], w, 1) != 1) {
      printf("%d: write error\r\n", getpid());
      exit(1);
    }

  } else {
    close(p1[0]);
    close(p2[1]);

    if (write(p1[1], w, 1) != 1) {
      printf("%d: write error\r\n", getpid());
      exit(1);
    }

    if (read(p2[0], r, 1) != 1) {
      printf("%d: read error\r\n", getpid());
      exit(1);
    }

    printf("%d: received pong\r\n", getpid());

    wait(0);
  }

  exit(0);
}