#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

void dfs(int p0) {
  int prime = 0;
  if (read(p0, &prime, 4) == 4) {
    printf("prime %d\r\n", prime);
  } else {
    close(p0);
    return;
  }

  int p[2];
  pipe(p);

  if (fork() == 0) {
    close(p[1]);

    dfs(p[0]);
  } else {
    close(p[0]);

    int num = 0;
    while (read(p0, &num, 4) == 4) {
      if (num % prime != 0) {
        if (write(p[1], &num, 4) == 0) {
          printf("%d: write error\r\n", getpid());
        }
      }
    }

    close(p0);
    close(p[1]);

    wait(0);
  }
}

#define MIN_NUM (2)
#define MAX_NUM (35)

int main(int argc, char *argv[]) {
  int p[2];
  pipe(p);

  if (fork() == 0) {
    close(p[1]);

    dfs(p[0]);
  } else {
    close(p[0]);

    for (int i = MIN_NUM; i <= MAX_NUM; i++) {
      if (write(p[1], &i, 4) == 0) {
        printf("%d: write error\r\n", getpid());
      }
    }
    close(p[1]);

    wait(0);
  }

  exit(0);
}