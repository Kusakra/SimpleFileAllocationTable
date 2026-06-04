#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "SFAT.h"
#include "file.h"
#include "user.h"
void fileMenu(void);
void list_root_files(void);
static DirEntry *find_entry_in_dir(Directory *dir, const char *name);
static unsigned int get_cluster_at_index(unsigned int start_cluster, unsigned int index);
static unsigned int get_last_cluster(unsigned int start_cluster);
// static unsigned int allocate_data_cluster(void);
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
    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    const char *name = path;
    if (name[0] == '/' || name[0] == '\\') {
        name++;
    }
    if (name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return -1;
    }

    Directory *dir = &sfat.rootDirectory;
    if (find_entry_in_dir(dir, name) != NULL) {
        return -1;
    }
    if (dir->count >= MAX_ROOT_FILES) {
        return -1;
    }

    unsigned int cluster = findFreeCluster();
    if (cluster == FAT_EOF) {
        return -1;
    }

    DirEntry *entry = &dir->entries[dir->count++];
    memset(entry, 0, sizeof(DirEntry));
    strncpy(entry->name, name, MAX_FILENAME_LENGTH - 1);
    entry->name[MAX_FILENAME_LENGTH - 1] = '\0';
    entry->type = 0;
    entry->startCluster = cluster;
    entry->size = 0;

    if (writeCluster(sfat.fat, FAT_START_CLUSTER, FAT_CLUSTERS) != 0) {
        return -1;
    }
    if (writeCluster(sfat.rootDirectory.entries, ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS) != 0) {
        return -1;
    }

    return 0;
}

int open_file(const char *path, FileMode mode, char user_id) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    const char *name = path;
    if (name[0] == '/' || name[0] == '\\') {
        name++;
    }
    if (name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return -1;
    }

    Directory *dir = &sfat.rootDirectory;
    DirEntry *entry = find_entry_in_dir(dir, name);
    if (!entry) {
        return -1;
    }

    int fd = alloc_fd();
    if (fd < 0) {
        return -1;
    }

    sfat.openFiles[fd].entry = entry;
    open_file_table[fd].start_cluster = entry->startCluster;
    open_file_table[fd].current_cluster = entry->startCluster;
    open_file_table[fd].position = (mode == FILE_MODE_APPEND) ? entry->size : 0;
    open_file_table[fd].size = entry->size;
    open_file_table[fd].mode = mode;
    open_file_table[fd].user_id = user_id;
    strncpy(open_file_table[fd].filename, name, MAX_FILENAME_LENGTH - 1);
    open_file_table[fd].filename[MAX_FILENAME_LENGTH - 1] = '\0';

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
    if (open_file_table[fd].mode != FILE_MODE_READ &&
        open_file_table[fd].mode != FILE_MODE_APPEND) {
        return -1;
    }

    DirEntry *entry = sfat.openFiles[fd].entry;
    if (!entry) {
        return -1;
    }

    if (open_file_table[fd].position >= entry->size) {
        return 0;
    }

    int to_read = size;
    int read_bytes = 0;
    unsigned int offset_in_file = open_file_table[fd].position;
    unsigned int current_cluster = get_cluster_at_index(entry->startCluster,
                                                        offset_in_file / CLUSTER_SIZE);
    if (current_cluster == FAT_EOF) {
        return 0;
    }

    while (to_read > 0 && offset_in_file < entry->size && current_cluster != FAT_EOF) {
        char *block = readCluster(current_cluster, 1);
        if (!block) {
            return -1;
        }

        unsigned int offset_in_cluster = offset_in_file % CLUSTER_SIZE;
        int chunk = CLUSTER_SIZE - offset_in_cluster;
        if (chunk > to_read) chunk = to_read;
        if (chunk > (int)(entry->size - offset_in_file)) {
            chunk = entry->size - offset_in_file;
        }

        memcpy((char *)buffer + read_bytes, block + offset_in_cluster, chunk);
        free(block);

        read_bytes += chunk;
        offset_in_file += chunk;
        to_read -= chunk;

        if (offset_in_file < entry->size) {
            current_cluster = sfat.fat[current_cluster];
        }
    }

    open_file_table[fd].position = offset_in_file;
    return read_bytes;
}

int write_file(int fd, const void *buffer, int size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_file_table[fd].in_use) {
        printf("[DEBUG] Invalid fd: %d\n", fd);
        return -1;
    }
    if (open_file_table[fd].mode == FILE_MODE_READ) {
        printf("[DEBUG] File opened in READ mode\n");
        return -1;
    }

    DirEntry *entry = sfat.openFiles[fd].entry;
    if (!entry) {
        printf("[DEBUG] Entry is NULL\n");
        return -1;
    }

    entry->size = size;
    writeFileToDisk(entry, buffer);
    // 写入文件由fileio.c中的writeFileToDisk函数替代，保持单一职责
    /*
    unsigned int current_cluster = entry->startCluster;
    unsigned int offset = 0;
    int to_write = size;
    int written = 0;

    while (to_write > 0 && current_cluster != FAT_EOF) {
        int chunk = (to_write > CLUSTER_SIZE) ? CLUSTER_SIZE : to_write;

        if (writeCluster((const char *)buffer + written, current_cluster, 1) != 0) {
            printf("[DEBUG] writeCluster failed at cluster %u\n", current_cluster);
            return -1;
        }

        written += chunk;
        to_write -= chunk;
        offset += chunk;

        if (to_write > 0) {
            if (sfat.fat[current_cluster] == FAT_EOF) {
                unsigned int next_cluster = findFreeCluster();
                if (next_cluster == FAT_EOF) {
                    printf("[DEBUG] Cannot allocate more clusters\n");
                    return -1;
                }
                sfat.fat[current_cluster] = next_cluster;
                current_cluster = next_cluster;
            } else {
                current_cluster = sfat.fat[current_cluster];
            }
        }
    }

    entry->size = offset;
    open_file_table[fd].size = offset;
    open_file_table[fd].position = offset;

    if (writeCluster(sfat.fat, FAT_START_CLUSTER, FAT_CLUSTERS) != 0) {
        printf("[DEBUG] FAT write failed\n");
        return -1;
    }
    if (writeCluster(sfat.rootDirectory.entries, ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS) != 0) {
        printf("[DEBUG] Root dir write failed\n");
        return -1;
    }

    printf("[INFO] Wrote %d bytes to file\n", written);
    */
    return 0;
}

int delete_file(const char *path, char user_id) {
    // 参数检查
    if (path == NULL || path[0] == '\0') {
        printf("[ERROR] Invalid path: path is NULL or empty\n");
        return -1;
    }

    const char *name = path;
    if (name[0] == '/' || name[0] == '\\') {
        name++;
    }
    if (name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        printf("[ERROR] Invalid path: contains subdirectory or invalid characters\n");
        return -1;
    }

    Directory *dir = &sfat.rootDirectory;
    
    // 查找文件
    DirEntry *entry = find_entry_in_dir(dir, name);
    if (!entry) {
        printf("[ERROR] File not found: %s\n", name);
        return -1;
    }

    // 检查文件是否被打开
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_file_table[i].in_use && 
            strncmp(open_file_table[i].filename, name, MAX_FILENAME_LENGTH) == 0) {
            printf("[ERROR] File is still open, cannot delete\n");
            return -1;
        }
    }

    // 释放文件占用的所有簇
    unsigned int cluster = entry->startCluster;
    printf("[DEBUG] Deleting file: %s, releasing clusters starting at %u\n", name, cluster);
    while (cluster != FAT_EOF && cluster != FAT_FREE) {
        unsigned int next_cluster = sfat.fat[cluster];
        sfat.fat[cluster] = FAT_FREE;
        printf("[DEBUG] Released cluster %u\n", cluster);
        if (sfat.freeClusterCount < MAX_CLUSTERS) {
            sfat.freeClusterCount++;
        }
        cluster = next_cluster;
    }

    // 删除目录项：找到这个 entry 在 dir->entries 中的位置
    int entry_index = -1;
    for (int i = 0; i < dir->count; i++) {
        if (&dir->entries[i] == entry) {
            entry_index = i;
            break;
        }
    }

    if (entry_index == -1) {
        printf("[ERROR] Failed to locate file in directory\n");
        return -1;
    }

    // 将最后一个目录项移到当前位置，然后减少计数
    if (entry_index < dir->count - 1) {
        memcpy(&dir->entries[entry_index], 
               &dir->entries[dir->count - 1], 
               sizeof(DirEntry));
    }
    dir->count--;

    // 写回 FAT 和根目录到磁盘
    if (writeFAT() != 0) {
        printf("[ERROR] Failed to write FAT to disk\n");
        return -1;
    }
    if (writeRootDirectory() != 0) {
        printf("[ERROR] Failed to write root directory to disk\n");
        return -1;
    }

    printf("[INFO] File deleted successfully: %s\n", name);
    return 0;
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

void fileMenu(void) {
    if (currentUserID == ID_NOT_LOGIN) {
        printf("[ERROR] Please login first!\n");
        return;
    }

    int choice;
    char path[256];
    int fd;
    char buffer[1024];
    int size;

    while (1) {
        printf("\n===== FILE MENU =====\n");
        printf("1. Create file\n");
        printf("2. Open file\n");
        printf("3. Close file\n");
        printf("4. Read file\n");
        printf("5. Write file\n");
        printf("6. Delete file\n");
        printf("7. Seek file\n");
        printf("8. List files\n");
        printf("0. Back to main menu\n");
        printf("====================\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        getchar();

        switch (choice) {
            case 1:
                printf("Enter file path: ");
                fgets(path, sizeof(path), stdin);
                path[strcspn(path, "\n")] = '\0';
                if (create_file(path, currentUserID) == 0) {
                    printf("[INFO] File created successfully.\n");
                } else {
                    printf("[ERROR] Failed to create file.\n");
                }
                break;

            case 2:
                printf("Enter file path: ");
                fgets(path, sizeof(path), stdin);
                path[strcspn(path, "\n")] = '\0';
                printf("Enter mode (0=READ, 1=WRITE, 2=APPEND): ");
                int mode;
                scanf("%d", &mode);
                getchar();
                fd = open_file(path, (FileMode)mode, currentUserID);
                if (fd >= 0) {
                    printf("[INFO] File opened successfully. FD=%d\n", fd);
                } else {
                    printf("[ERROR] Failed to open file.\n");
                }
                break;

            case 3:
                printf("Enter file descriptor: ");
                scanf("%d", &fd);
                getchar();
                if (close_file(fd) == 0) {
                    printf("[INFO] File closed successfully.\n");
                } else {
                    printf("[ERROR] Failed to close file.\n");
                }
                break;

            case 4:
                printf("Enter file descriptor: ");
                if (scanf("%d", &fd) != 1) {
                    printf("[ERROR] Invalid file descriptor.\n");
                    while (getchar() != '\n');  // 清空缓冲区
                    break;
                }
                printf("Enter size to read: ");
                if (scanf("%d", &size) != 1) {
                    printf("[ERROR] Invalid size.\n");
                    while (getchar() != '\n');
                    break;
                }
                getchar();
                int read_bytes = read_file(fd, buffer, size);
                if (read_bytes >= 0) {
                    printf("[INFO] Read %d bytes: %.*s\n", read_bytes, read_bytes, buffer);
                } else {
                    printf("[ERROR] Failed to read file.\n");
                }
                break;

            case 5:
                printf("Enter file descriptor: ");
                if (scanf("%d", &fd) != 1) {
                    printf("[ERROR] Invalid file descriptor.\n");
                    while (getchar() != '\n');  // 清空缓冲区
                    break;
                }
                printf("Enter data to write: ");
                getchar();
                fgets(buffer, sizeof(buffer), stdin);
                buffer[strcspn(buffer, "\n")] = '\0';
                if (write_file(fd, buffer, strlen(buffer)) == 0) {
                    printf("[INFO] Data written successfully.\n");
                } else {
                    printf("[ERROR] Failed to write file.\n");
                }
                break;

            case 6:
                printf("Enter file path: ");
                fgets(path, sizeof(path), stdin);
                path[strcspn(path, "\n")] = '\0';
                if (delete_file(path, currentUserID) == 0) {
                    printf("[INFO] File deleted successfully.\n");
                } else {
                    printf("[ERROR] Failed to delete file.\n");
                }
                break;

            case 7:
                printf("Enter file descriptor: ");
                scanf("%d", &fd);
                printf("Enter offset: ");
                int offset;
                scanf("%d", &offset);
                printf("Enter whence (0=SET, 1=CUR, 2=END): ");
                int whence;
                scanf("%d", &whence);
                getchar();
                int pos = file_seek(fd, offset, whence);
                if (pos >= 0) {
                    printf("[INFO] Seek successful. New position: %d\n", pos);
                } else {
                    printf("[ERROR] Failed to seek file.\n");
                }
                break;
            case 8:
                list_root_files();
                break;

            case 0:
                return;

            default:
                printf("[ERROR] Invalid choice!\n");
        }
    }
}

static DirEntry *find_entry_in_dir(Directory *dir, const char *name) {
    for (int i = 0; i < dir->count; i++) {
        if (strncmp(dir->entries[i].name, name, MAX_FILENAME_LENGTH) == 0) {
            return &dir->entries[i];
        }
    }
    return NULL;
}

static unsigned int get_cluster_at_index(unsigned int start_cluster, unsigned int index) {
    unsigned int cluster = start_cluster;
    for (unsigned int i = 0; i < index && cluster != FAT_EOF; i++) {
        cluster = sfat.fat[cluster];
    }
    return cluster;
}

static unsigned int get_last_cluster(unsigned int start_cluster) {
    unsigned int cluster = start_cluster;
    while (cluster != FAT_EOF && sfat.fat[cluster] != FAT_EOF) {
        cluster = sfat.fat[cluster];
    }
    return cluster;
}

// 寻找空闲簇由fileio.c中的findFreeCluster函数替代，保持单一职责
// static unsigned int allocate_data_cluster(void) {
//     unsigned int firstDataCluster = ROOT_DIR_START_CLUSTER + ROOT_DIR_CLUSTERS;
//     for (unsigned int i = firstDataCluster; i < MAX_CLUSTERS; i++) {
//         if (sfat.fat[i] == FAT_FREE) {
//             sfat.fat[i] = FAT_EOF;
//             return i;
//         }
//     }
//     return FAT_EOF;
// }

void list_root_files(void) {
    Directory *dir = &sfat.rootDirectory;
    printf("\n[INFO] Root directory files: %d\n", dir->count);
    for (int i = 0; i < dir->count; i++) {
        DirEntry *entry = &dir->entries[i];
        printf("%2d: %-16s size=%u startCluster=%u type=%s\n",
               i,
               entry->name,
               entry->size,
               entry->startCluster,
               entry->type == SUBDIR ? "DIR" : "FILE");
    }
}