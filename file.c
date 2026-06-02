#include <string.h>
#include <stdio.h>
#include "SFAT.h"
#include "file.h"
#include "user.h"

static OpenFileEntry open_file_table[MAX_OPEN_FILES];

int init_open_file_table(void) {
    memset(open_file_table, 0, sizeof(open_file_table));
    return 0;
}

static int alloc_fd(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_file_table[i].in_use) {
            open_file_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

int create_file(const char *path, char user_id) {
    // TODO: 调用 dir.c 中的函数创建目录项
    // 需要分配新簇并在目录中添加条目
    return -1;
}

int open_file(const char *path, FileMode mode, char user_id) {
    // TODO: 调用 dir.c 中的函数查找文件
    // 从目录项中获取 start_cluster 和 size
    DirEntry *entry = NULL;
    
    if (!entry) {
        return -1;
    }

    int fd = alloc_fd();
    if (fd < 0) {
        return -1;
    }

    open_file_table[fd].start_cluster = entry->startCluster;
    open_file_table[fd].current_cluster = entry->startCluster;
    open_file_table[fd].position = (mode == FILE_MODE_APPEND) ? entry->size : 0;
    open_file_table[fd].size = entry->size;
    open_file_table[fd].mode = mode;
    open_file_table[fd].user_id = user_id;
    strncpy(open_file_table[fd].filename, path, MAX_FILENAME_LENGTH - 1);

    return fd;
}

int close_file(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_file_table[fd].in_use) {
        return -1;
    }
    open_file_table[fd].in_use = 0;
    return 0;
}

int read_file(int fd, void *buffer, int size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_file_table[fd].in_use) {
        return -1;
    }
    if (open_file_table[fd].mode != FILE_MODE_READ && open_file_table[fd].mode != FILE_MODE_APPEND) {
        return -1;
    }

    int to_read = size;
    int read_bytes = 0;
    char *block = NULL;

    unsigned int current_cluster = open_file_table[fd].start_cluster;
    unsigned int offset_in_file = open_file_table[fd].position;

    while (to_read > 0 && offset_in_file < open_file_table[fd].size) {
        // TODO: 根据 offset_in_file 计算应该读取哪个簇
        // 可以通过遍历 FAT 链或其他方式实现
        
        block = readCluster(current_cluster, 1);
        if (!block) {
            return -1;
        }

        int offset_in_cluster = offset_in_file % CLUSTER_SIZE;
        int chunk = CLUSTER_SIZE - offset_in_cluster;
        if (chunk > to_read) chunk = to_read;
        if (chunk > open_file_table[fd].size - offset_in_file) {
            chunk = open_file_table[fd].size - offset_in_file;
        }

        memcpy((char *)buffer + read_bytes, block + offset_in_cluster, chunk);
        read_bytes += chunk;
        offset_in_file += chunk;
        to_read -= chunk;
        
        // TODO: 获取 FAT 中的下一个簇号
        // current_cluster = sfat.fat[current_cluster];
        // if (current_cluster == FAT_EOF) break;
    }

    open_file_table[fd].position = offset_in_file;
    return read_bytes;
}

int write_file(int fd, const void *buffer, int size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_file_table[fd].in_use) {
        return -1;
    }
    if (open_file_table[fd].mode == FILE_MODE_READ) {
        return -1;
    }

    sfat.openFiles[fd].entry->size = size; // 更新目录项中的文件大小
    return writeFileToDisk(sfat.openFiles[fd].entry, buffer);
}

int delete_file(const char *path, char user_id) {
    // TODO: 调用 dir.c 中的函数删除文件
    // 需要释放文件占用的簇链
    return -1;
}

int file_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_file_table[fd].in_use) {
        return -1;
    }

    int newpos = open_file_table[fd].position;
    if (whence == SEEK_SET) newpos = offset;
    else if (whence == SEEK_CUR) newpos += offset;
    else if (whence == SEEK_END) newpos = open_file_table[fd].size + offset;

    if (newpos < 0 || newpos > open_file_table[fd].size) {
        return -1;
    }
    open_file_table[fd].position = newpos;
    return newpos;
}

int get_open_file_size(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_file_table[fd].in_use) return -1;
    return open_file_table[fd].size;
}