#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"SFAT.h"

// 系统初始化，加载变量，加载磁盘
int init() {
    // 初始化全局变量
    currentUserRole = ROLE_NOT_LOGIN; // 初始化当前用户角色为未登录
    currentUserID = ID_NOT_LOGIN; // 初始化当前用户ID为未登录
    cdi = 0; // 初始化当前目录指针索引
    NULL_FILE.modify_flag = 0; // 初始化空文件结构体的修改标志为0
    NULL_FILE.entry = NULL; // 初始化空文件结构体的目录项指针为NULL
    NULL_USER.role = ROLE_NULL; // 初始化空用户结构体的角色为未使用
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        sfat.openFiles[i] = NULL_FILE; // 初始化打开文件表中的每个文件结构体为NULL_FILE
    }
    for (int i = 0; i < MAX_USERS; i++) {
        sfat.Users[i] = NULL_USER; // 初始化用户列表中的每个用户结构体为NULL_USER
    }

    // 打开或创建磁盘文件
    sfat.fd = fopen("disk.img", "rw+b"); // 打开磁盘文件
    if (sfat.fd == NULL) {
        logger("Disk image not found. Creating a new one...", LOG_INFO);

        sfat.fd = fopen("disk.img", "w+b"); // 如果文件不存在则创建新文件
        if (sfat.fd == NULL) {
            logger("Failed to open disk image.", LOG_ERROR);
            return 1;
        }
        format(); // 格式化磁盘
        logger("Disk image created and formatted.", LOG_INFO);
    }
    else {
        logger("Disk image found. Initializing...", LOG_INFO);
    }

    load(); // 初始化内存结构，避免后续操作空指针
    logger("Initialization complete.", LOG_INFO);
    return 0;
}

// 加载磁盘到内存
int load() {
    char *buf;

    // 从磁盘读取配置区（#1）
    buf = (char *)readCluster(1, 1); // 从磁盘读取配置区数据到缓冲区    
    memcpy(&sfat.nextFreeCluster, buf, sizeof(unsigned int)); // 从配置区数据中读取下一个空闲簇号
    memcpy(&sfat.freeClusterCount, buf + 4, sizeof(unsigned int)); // 从配置区数据中读取空闲簇数量
    free(buf); // 释放配置区缓冲区

    // 从磁盘读取FAT表到内存
    sfat.fat = (unsigned int *)readCluster(FAT_START_CLUSTER, FAT_CLUSTERS); // 从磁盘读取FAT表到内存

    // 初始化根目录
    sfat.rootDirectory.entries = (DirEntry *)calloc(MAX_ROOT_FILES, sizeof(DirEntry)); // 分配根目录项数组
    sfat.rootDirectory.name = ROOT_DIR; // 设置根目录名称
    sfat.rootDirectory.count = 0; // 初始化根目录项数量为0
    // 读取根目录
    buf = readCluster(ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS); // 从磁盘读取根目录数据到缓冲区
    DirEntry *entry = (DirEntry *)buf; // 将缓冲区数据解释
    while (entry->name[0] != UNUSED) { // 循环读取根目录项，直到遇到未使用标志
        if (entry->name[0] == DELETED) { // 跳过已删除的目录项
            entry = (DirEntry *)((char *)entry + DIRENTRY_SIZE);
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

    return 0;
}

// 初始化磁盘，格式化文件系统
int format()
{
    char *buf;

    // 初始化配置区（#1）
    buf = (char *)calloc(CLUSTER_SIZE, 1); // 分配并初始化配置区缓冲区
    memcpy(buf, &(unsigned int){24}, sizeof(unsigned int)); // 数据区起始簇号
    memcpy(buf + 4, &(unsigned int){MAX_CLUSTERS - 24}, sizeof(unsigned int)); // 数据区空闲簇数量
    writeCluster(buf, 1, 1); // 将配置区数据写入磁盘
    free(buf); // 释放配置区缓冲区

    // 初始化用户表（#3）
    User admin;
    strcpy(admin.username, "admin");
    strcpy(admin.password, "admin");
    admin.role = ROLE_ADMIN; // 管理员角色
    admin.userid = 0x01;    // 角色id从01开始

    buf = (char *)calloc(CLUSTER_SIZE, 1); // 分配并初始化用户表缓冲区
    memcpy(buf, &admin, sizeof(User)); // 将管理员用户数据复制到缓冲区
    writeCluster(buf, USER_TABLE_CLUSTER, 1); // 将用户表写入磁盘
    free(buf); // 释放用户表缓冲区

    // 初始化FAT表（#4-19）
    sfat.fat = (unsigned int *)calloc(FAT_CLUSTERS * CLUSTER_SIZE, 1); // 分配并初始化FAT表内存
    writeCluster(sfat.fat, FAT_START_CLUSTER, FAT_CLUSTERS); // 写入FAT表到磁盘，初始状态为全0表示所有簇空闲
    
    // 初始化根目录（#20-23）
    buf = (char *)calloc(MAX_ROOT_FILES * DIRENTRY_SIZE, 1); // 分配并初始化根目录缓冲区
    writeCluster(buf, ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS); // 将根目录写入磁盘，初始状态为全0表示没有文件
    free(buf); // 释放根目录缓冲区
    
    // 数据区（#24-...）已经在FAT表中设置为FAT_FREE，无需额外操作
    buf = (char *)calloc(CLUSTER_SIZE, 1); // 分配并初始化数据区缓冲区
    writeCluster(buf, MAX_CLUSTERS - 1, 1); // 将数据区写入磁盘，初始状态为全0表示所有簇空闲
    free(buf); // 释放数据区缓冲区
    return 0;
}