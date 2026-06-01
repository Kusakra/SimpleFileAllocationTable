#include<stdio.h>
#include<string.h>
#include"SFAT.h"

// 从磁盘读取目录，构建目录结构并返回目录结构指针
Directory* dirFromDisk(unsigned int cluster) {
    Directory *dir = (Directory *)malloc(sizeof(Directory)); // 分配目录结构体
    // 实际单个目录项内存占用小于64字节
    dir->entries = (DirEntry *)calloc(MAX_ROOT_FILES, sizeof(DirEntry)); // 分配目录项数组并初始化为0
    dir->count = 0; // 初始化目录项数量为0

    char *buf = (char *)malloc(CLUSTER_SIZE); // 分配缓冲区
    DirEntry *entry;
    // 循环读取目录簇链，直到遇到FAT_EOF标志
    while (cluster != FAT_EOF) {
        buf = readCluster(cluster, 1); // 读取当前簇数据到缓冲区
        entry = (DirEntry *)buf; // 将缓冲区数据解释为目录项数组
        cluster = sfat.fat[cluster]; // 获取下一个簇号
        
        // 循环读取目录项
        while(entry->name[0] != UNUSED) {
            if (entry->name[0] == DELETED) {  // 0xE5表示已删除，跳过
                continue;
            }
            memcpy(&dir->entries[dir->count++], entry, sizeof(DirEntry)); // 将目录项复制到目录结构体中
            entry = (DirEntry *)((char *)entry + DIRENTRY_SIZE); // 移动到下一个目录项
        }
    }
    free(buf); // 释放缓冲区
    return dir;
}

// 列出目录内容
int dir(const char *path) {

}

// 创建目录
int mkdir(const char *name) {
    
}

// 删除目录
int rmdir(const char *name) {

}

// 切换目录
int cd(const char *path) {

}