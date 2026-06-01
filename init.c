#include<stdio.h>
#include<stdlib.h>
#include"SFAT.h"

int init() {
    // 从磁盘读取FAT表到内存
    sfat.fat = readCluster(FAT_START_CLUSTER, FAT_CLUSTERS); // 从磁盘读取FAT表到内存

    // 初始化根目录
    sfat.rootDirectory.entries = (DirEntry *)calloc(MAX_ROOT_FILES, sizeof(DirEntry)); // 分配根目录项数组
    sfat.rootDirectory.count = 0; // 初始化根目录项数量为0
    // 读取根目录
    char *buf = readCluster(ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS); // 从磁盘读取根目录数据到缓冲区
    DirEntry *entry = (DirEntry *)buf; // 将缓冲区数据解释
    while (entry->name[0] != UNUSED) { // 循环读取根目录项，直到遇到未使用标志
        if (entry->name[0] == DELETED) { // 跳过已删除的目录项
            continue;
        }
        memcpy(&sfat.rootDirectory.entries[sfat.rootDirectory.count++], entry, sizeof(DirEntry));
        entry = (DirEntry *)((char *)entry + DIRENTRY_SIZE); // 移动到下一个目录项
    }
    free(buf); // 释放缓冲区

    // 初始化目录栈，初始时只有根目录
    sfat.dirStack[0] = sfat.rootDirectory; // 将根目录压入目录栈

    // 初始化用户表
    buf = readCluster(USER_TABLE_CLUSTER, 1); // 从磁盘读取用户表数据到缓冲区
    for (int i = 0; i < MAX_USERS; i++) {
        memcpy(&sfat.Users[i], buf + i * USER_SIZE, sizeof(User)); // 将用户表数据复制到内存
    }
    free(buf); // 释放缓冲区
}