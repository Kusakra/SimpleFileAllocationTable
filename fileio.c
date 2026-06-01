#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"SFAT.h"

// 说明：按簇读写硬盘，先在内存中构建簇数据，最后一次性写入磁盘，减少磁盘I/O次数，提高性能

// 从磁盘读取n个簇的数据并返回缓冲区指针，调用者负责释放内存
char *readCluster(unsigned int cluster, unsigned int n) {
    char *buf = (char *)malloc(n * CLUSTER_SIZE); // 分配缓冲区
    fseek(sfat.fd, cluster * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到指定簇的起始位置
    fread(buf, 1, n * CLUSTER_SIZE, sfat.fd); // 从磁盘读取n个簇的数据到缓冲区
    return buf;
}

// 将缓冲区的数据写入磁盘指定簇，n为簇数
int writeCluster(const void *buf, unsigned int cluster, unsigned int n) {
    fseek(sfat.fd, cluster * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到指定簇的起始位置
    fwrite(buf, 1, n * CLUSTER_SIZE, sfat.fd); // 将缓冲区的数据写入磁盘
    return 0;
}

// 寻找一个空闲簇，返回簇号，如果没有空闲簇则返回FAT_EOF
unsigned int findFreeCluster() {
    if (sfat.freeClusterCount == 0) { // 如果没有空闲簇，直接返回FAT_EOF
        return FAT_EOF;
    }
    if (sfat.freeClusterCount == 1) {
        sfat.freeClusterCount--; // 更新空闲簇数量
        unsigned int tmp = sfat.nextFreeCluster; // 获取下一个空闲簇号
        sfat.nextFreeCluster = FAT_EOF; // 更新下一个空闲簇号为FAT_EOF，表示没有更多空闲簇
        return tmp; // 返回最后一个空闲簇号
    }


    unsigned int allocatedCluster = sfat.nextFreeCluster;
    unsigned int nextCluster = sfat.fat[allocatedCluster]; // 获取下一个空闲簇号
    

    return FAT_EOF; // 没有空闲簇
}


// 将文件数据写入磁盘，entry为文件的目录项指针，buf为文件数据缓冲区
int writeFileToDisk(DirEntry *entry, const void *buf) {
    unsigned int cluster = entry->startCluster; // 获取文件数据的起始簇号
    unsigned int size = entry->size; // 获取文件大小
    unsigned int clustersNeeded = (size + CLUSTER_SIZE - 1) / CLUSTER_SIZE; // 计算需要的簇数，size向上取整

    unsigned int clusters[clustersNeeded]; // 存储分配的簇号
    int idx = 0; // 分配簇的索引
    // 分配簇链
    while (cluster != FAT_EOF) { // 循环分配簇，直到分配足够的簇或遇到FAT_EOF
        clusters[idx++] = cluster; // 将当前簇号存储到数组中
        cluster = sfat.fat[cluster]; // 获取下一个簇号
    }
    if (idx < clustersNeeded) { // 文件变大，需要分配更多的簇
        for (int i = 0; i < clustersNeeded - idx; i++) {
            unsigned int newCluster = findFreeCluster(); // 查找一个空闲簇
            if (newCluster == FAT_EOF) { // 如果没有空闲簇，返回错误
                logger("No free cluster available.", LOG_ERROR);
                return -1;
            }
            sfat.fat[clusters[idx - 1 + i]] = newCluster; // 将前一个簇的FAT项指向新簇
            sfat.fat[newCluster] = FAT_EOF; // 将新簇的FAT项设置为FAT_EOF
            clusters[idx + i] = newCluster; // 将新簇号存储到数组中
        }
    }
    else if (cluster != FAT_EOF) {  // 文件变小，释放多余的簇
        for (unsigned int i = clustersNeeded; i < idx; i++) {
            sfat.fat[clusters[i]] = FAT_FREE; // 将多余簇的FAT项设置为FAT_FREE
        }
    }

    for (unsigned int i = 0; i < clustersNeeded; i++) {
        writeCluster((char *)buf + i * CLUSTER_SIZE, cluster, 1); // 将数据写入当前簇
        cluster = sfat.fat[cluster]; // 获取下一个簇号
    }
    return 0;
}

// 保存完整数据到磁盘
int saveToDisk() {
    // 写入FAT表
    writeCluster(sfat.fat, FAT_START_CLUSTER, FAT_CLUSTERS); // 将FAT表写入磁盘

    // 写入根目录
    char *buf = (char *)calloc(MAX_ROOT_FILES * DIRENTRY_SIZE, 1); // 分配并初始化根目录缓冲区
    for (int i = 0; i < sfat.rootDirectory.count; i++) {
        memcpy(buf + i * DIRENTRY_SIZE, &sfat.rootDirectory.entries[i], sizeof(DirEntry)); // 将根目录项复制到缓冲区
    }
    writeCluster(buf, ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS); // 将根目录写入磁盘
    free(buf); // 释放根目录缓冲区

    // 写入用户表
    buf = (char *)calloc(MAX_USERS * USER_SIZE, 1); // 分配并初始化用户表缓冲区
    for (int i = 0; i < MAX_USERS; i++) {
        memcpy(buf + i * USER_SIZE, &sfat.Users[i], sizeof(User)); // 将用户表数据复制到缓冲区
    }
    writeCluster(buf, USER_TABLE_CLUSTER, 1); // 将用户表写入磁盘
    free(buf); // 释放用户表缓冲区

    logger("USER TABLE SAVED.", LOG_INFO); // 记录日志
    // 写入文件
 
    return 0;
}