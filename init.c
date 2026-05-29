#include<stdio.h>
#include"SFAT.h"

int init() {

    sfat.fat = (unsigned int *)malloc(FAT_CLUSTERS * CLUSTER_SIZE); // 分配FAT表内存
    fseek(sfat.fd, FAT_START_CLUSTER * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到FAT表的起始位置
    fread(sfat.fat, 1, FAT_CLUSTERS * CLUSTER_SIZE, sfat.fd); // 从磁盘读取FAT表到内存

    sfat.rootDirectory.entries = (DirEntry *)malloc(MAX_ROOT_FILES * DIRENTRY_SIZE); // 分配根目录项数组
    fseek(sfat.fd, ROOT_DIR_START_CLUSTER * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到根目录的起始位置
    fread(sfat.rootDirectory.entries, 1, ROOT_DIR_CLUSTERS * CLUSTER_SIZE, sfat.fd); // 从磁盘读取根目录到内存

}