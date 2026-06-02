#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include "SFAT.h"

typedef enum {
    FILE_MODE_READ,
    FILE_MODE_WRITE,
    FILE_MODE_APPEND
} FileMode;

typedef struct {
    int in_use;
    unsigned int start_cluster;
    unsigned int current_cluster;
    unsigned int position;
    unsigned int size;
    FileMode mode;
    char user_id;
    char filename[MAX_FILENAME_LENGTH];
} OpenFileEntry;

int init_open_file_table(void);
int create_file(const char *path, char user_id);
int open_file(const char *path, FileMode mode, char user_id);
int close_file(int fd);
int read_file(int fd, void *buffer, int size);
int write_file(int fd, const void *buffer, int size);
int delete_file(const char *path, char user_id);
int file_seek(int fd, int offset, int whence);
int get_open_file_size(int fd);
void fileMenu(void);

#endif // FILE_H