#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *filename){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){//
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){//将打开文件fd的信息放入*st
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }
    //第一个参数必须是目录
    if(st.type != T_DIR){
        fprintf(2,"usage: find <directory> <filename>\n");
        return;
    }
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){//?
      fprintf(2, "finds: path too long\n");
      return ;
    }
    strcpy(buf, path);//
    p = buf + strlen(buf);
    *p++ = '/';//指向最后一个/之后
    while(read(fd, &de, sizeof de) == sizeof de){//
        if(de.inum == 0){
            continue;
        }
        memmove(p, de.name, DIRSIZ);//添加路径名称
        p[DIRSIZ] = 0;//
        if(stat(buf,&st) < 0){//将指定名称的文件信息放入*st
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }
        //不在"."和".."中递归
        if(st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0){
            find(buf, filename);
        }else if(strcmp(filename, p) == 0){
            printf("%s\n", buf);
        }
    }
    close(fd);
}

int main(int argc, char *argv[]){
    if(argc != 3){
        fprintf(2, "usage: find <directory> <filename>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}