#include<stdio.h>

#include"SFAT.h"


Directory rootDirectory; // 根目录
Directory *dirStack[MAX_STACK_DEPTH]; // 目录栈
OpenFile openFiles[MAX_OPEN_FILES]; // 打开文件表
User Users[MAX_USERS]; // 用户列表
FILE *fd; // 磁盘文件指针
char currentUserID; // 当前用户ID
FAT fat; // FATs表

int main()
{
    
    printf("Hello, World!\n");
    return 0;
}