#include "kernel/types.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if (argc != 1) {
    exit(-1);
  }
  int f2c[2];
  int c2f[2];
  pipe(f2c);
  pipe(c2f);
  int pid = fork();
  if(pid == 0){
    close(f2c[1]);
    close(c2f[0]);
    char buf[4];
    read(f2c[0],buf,4);
    printf("%d: received ping from pid %s\n",getpid(),buf);
    itoa(getpid(),buf);
    write(c2f[1],buf,4);
    exit(0);
  }else{
    close(f2c[0]);
    close(c2f[1]);
    char buf[4];
    itoa(getpid(),buf);
    write(f2c[1],buf,4);
    wait(&pid);
    read(c2f[0],buf,4);
    printf("%d: received pong from pid %s\n",getpid(),buf);
  }
  exit(0);
}