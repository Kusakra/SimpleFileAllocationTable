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