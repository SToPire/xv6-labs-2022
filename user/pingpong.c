#include "kernel/types.h"
#include "user/user.h"

#define READ_END 0
#define WRITE_END 1

int main(int argc, char *argv[]) {
  int p2c[2], c2p[2];
  char *c;

  if (pipe(p2c) < 0) {
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  if (pipe(c2p) < 0) {
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  if (fork() == 0) {
    // child
    close(p2c[WRITE_END]); // close the write end of 'parent to child' pipe
    close(c2p[READ_END]);

    if (read(p2c[READ_END], &c, 1) != 1) {
      fprintf(2, "read failed\n");
      exit(1);
    }

    printf("%d: received ping\n", getpid());
    if (write(c2p[WRITE_END], c, 1) != 1) {
      fprintf(2, "write failed\n");
      exit(1);
    }

    exit(0);
  }

  // parent
  close(p2c[READ_END]);
  close(c2p[WRITE_END]);

  c = "?";
  if (write(p2c[WRITE_END], c, 1) != 1) {
    fprintf(2, "write failed\n");
    exit(1);
  }

  if (read(c2p[READ_END], &c, 1) != 1) {
    fprintf(2, "read failed\n");
    exit(1);
  }
  printf("%d: received pong\n", getpid());

  exit(0);
}