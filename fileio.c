#include<stdio.h>
#include<stdlib.h>
#include"SFAT.h"

// 从磁盘读取n个簇的数据并返回缓冲区指针，调用者负责释放内存
char *readCluster(unsigned int cluster, unsigned int n) {
    char *buf = (char *)malloc(n * CLUSTER_SIZE); // 分配缓冲区
    fseek(sfat.fd, cluster * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到指定簇的起始位置
    fread(buf, 1, n * CLUSTER_SIZE, sfat.fd); // 从磁盘读取n个簇的数据到缓冲区
    return buf;
}

// 将缓冲区的数据写入磁盘指定簇，n为簇数
int writeCluster(const char *buf, unsigned int cluster, unsigned int n) {
    fseek(sfat.fd, cluster * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到指定簇的起始位置
    fwrite(buf, 1, n * CLUSTER_SIZE, sfat.fd); // 将缓冲区的数据写入磁盘
    return 0;
}