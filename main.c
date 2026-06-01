#include<stdio.h>

#include"SFAT.h"

SFAT sfat; // 定义全局SFAT结构体实例
char currentUserID; // 当前用户ID
unsigned short cdi; // 当前目录指针索引currentDirectoryIndex，结合目录栈sfat.dirStack[cdi]

int main()
{
    sfat.fd = fopen("disk.img", "rw+b"); // 打开磁盘文件
    if (sfat.fd == NULL) {
        printf("Disk image not found. Creating a new one...\n");

        sfat.fd = fopen("disk.img", "w+b"); // 如果文件不存在则创建新文件
        if (sfat.fd == NULL) {
            perror("Failed to open disk image");
            return 1;
        }
        format(); // 格式化磁盘
    }
    else {
        printf("Disk image found. Initializing...\n");
        init(); // 初始化SFAT结构体
    }
    
    return 0;
}