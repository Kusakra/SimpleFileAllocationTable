#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"SFAT.h"

int format()
{
    // 初始化用户表（#3）
    User admin;
    strcpy(admin.username, "admin");
    strcpy(admin.password, "admin123");
    admin.role = 0x01; // 管理员角色
    admin.userid = 0x01;

    fseek(sfat.fd, USER_TABLE_CLUSTER * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到用户表的起始位置
    fwrite(&admin, USER_SIZE, 1, sfat.fd); // 将管理员用户写入磁盘

    // 初始化FAT表（#4-19）
    char *buf = (char *)calloc(FAT_CLUSTERS * CLUSTER_SIZE, 1); // 分配并初始化FAT缓冲区
    writeCluster(buf, FAT_START_CLUSTER, FAT_CLUSTERS); // 写入FAT表到磁盘，初始状态为全0表示所有簇空闲
    free(buf); // 释放FAT缓冲区
    sfat.fat = (unsigned int *)malloc(FAT_CLUSTERS * CLUSTER_SIZE); // 分配FAT表内存
    
    // 初始化根目录（#20-23）
    fseek(sfat.fd, ROOT_DIR_START_CLUSTER * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到根目录的起始位置
    fwrite(UNUSED, 1, 1, sfat.fd); // 设置根目录的第一个字节为0x00，表示未使用
    
    // 数据区（#24-...）已经在FAT表中设置为FAT_FREE，无需额外操作
    return 0;
}