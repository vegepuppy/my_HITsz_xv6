#include "kernel/types.h"
#include "user.h"
#include "kernel/fs.h"
#include "kernel/stat.h"

void find(char *path, char *target) {
  char buf[512], *p;
  int fd;
  struct stat st;
  struct dirent de;
  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  strcpy(buf,path);
  p = buf+strlen(buf);
  *p++ = '/';
  while(read(fd,&de,sizeof(de)) == sizeof(de)){
    if(de.inum == 0) continue;
    if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;
    strcpy(p,de.name);
    if(stat(buf,&st) >= 0){
        if(st.type == T_FILE){
            if(strcmp(target,de.name) == 0){
                printf("%s\n",buf);
            }
        }else if(st.type == T_DIR){
            if(strcmp(target,de.name) == 0){
                printf("%s\n",buf);
            }
            find(buf,target);
        }
    }
  }
  close(fd);
  return;
}
int main(int argc, char *argv[]) {
    find(argv[1],argv[2]);
    exit(0);
}