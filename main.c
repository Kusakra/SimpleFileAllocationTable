#include<stdio.h>
#include"SFAT.h"
#include"file.h"
#include"user.h"

SFAT sfat; // 定义全局SFAT结构体实例
char currentUserID; // 当前用户ID
unsigned short cdi; // 当前目录指针索引currentDirectoryIndex，结合目录栈sfat.dirStack[cdi]
OpenFile NULL_FILE; // 定义一个全局的空文件结构体实例，表示无效的文件
User NULL_USER; // 定义一个全局的空用户结构体实例，表示无效的用户
char LOG_STATUS;// 定义一个全局的日志状态变量，0表示关闭日志，1表示开启日志

int main()
{
    LOG_STATUS = LOG_INFO; // 设置日志状态为信息级别
    init(); // 初始化系统，加载变量，加载磁盘
    init_open_file_table();
    init_user_system();

    // 菜单循环
    int choice;
    while (1) {
        printf("\n===== SFAT FILE SYSTEM MENU =====\n");
        printf("1. File Operations\n");
        printf("2. User Operations\n");
        printf("0. Exit\n");
        printf("=================================\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        getchar();  // consume newline

        switch (choice) {
            case 1:
                // 调用 file 模块的菜单
                fileMenu();
                break;
            case 2:
                // 调用 user 模块的菜单
                userMenu();
                break;
            case 0:
                printf("[INFO] Saving to disk and exiting...\n");
                saveToDisk();
                return 0;
            default:
                printf("[ERROR] Invalid choice!\n");
        }
    }

    saveToDisk();
    return 0;
}

void logger(const char *message, char level) {
    if (LOG_STATUS == LOG_NULL) {
        return; // 如果日志关闭，直接返回
    }

    switch (level) {
        case LOG_ERROR:
            printf("[ERROR] %s\n", message);
        case LOG_WARNING:
            printf("[WARNING] %s\n", message);
        case LOG_INFO:
            printf("[INFO] %s\n", message);
            break;
        default:
            printf("[UNKNOWN] %s\n", message);
            break;
    }
}