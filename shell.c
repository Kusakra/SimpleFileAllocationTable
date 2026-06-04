/**
 * shell.c - 命令行解释器（成员4编写）
 * 
 * 功能：解析用户输入的命令，调用相应的文件系统接口
 */

#include "SFAT.h"
#include "shell.h"
#include "user.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 命令最大长度
#define CMD_MAX_LEN     512
#define ARG_MAX_COUNT   16

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 256
#endif

// 错误码定义（如果SFAT.h中没有，这里补充）
#ifndef SUCCESS
#define SUCCESS         0
#define ERR_NOT_FOUND   -1
#define ERR_EXIST       -2
#define ERR_NO_SPACE    -3
#define ERR_PERMISSION  -4
#define ERR_INVALID     -5
#define ERR_NOT_EMPTY   -6
#endif

// 文件模式转换（shell参数 -> FileMode）
#define FILE_MODE_READ      0
#define FILE_MODE_WRITE     1
#define FILE_MODE_APPEND    2

// 声明成员2的函数（下划线命名）
extern int cd(const char *path);
extern int dir(const char *path);
extern int mkdir(const char *name);
extern int rmdir(const char *name);
extern char* getcwd(char* buf, int size);

// 声明成员3的函数（下划线命名）
extern int init_open_file_table(void);
extern int create_file(const char *path, char user_id);
extern int open_file(const char *path, int mode, char user_id);
extern int close_file(int fd);
extern int read_file(int fd, void *buffer, int size);
extern int write_file(int fd, const void *buffer, int size);
extern int delete_file(const char *path, char user_id);
extern int file_seek(int fd, int offset, int whence);
extern int get_open_file_size(int fd);

/**
 * 检查命令是否无需登录即可执行
 */
static int is_public_command(const char *cmd) {
    if (cmd == NULL) return 0;
    
    // 允许无需登录的命令列表
    if (strcmp(cmd, "login") == 0) return 1;
    if (strcmp(cmd, "help") == 0) return 1;
    if (strcmp(cmd, "?") == 0) return 1;
    if (strcmp(cmd, "exit") == 0) return 1;
    if (strcmp(cmd, "quit") == 0) return 1;
    if (strcmp(cmd, "clear") == 0) return 1;
    
    return 0;
}

/**
 * 错误信息映射
 */
static const char* error_to_string(int err) {
    switch (err) {
        case SUCCESS:       return "成功";
        case ERR_NOT_FOUND: return "文件或目录不存在";
        case ERR_EXIST:     return "文件或目录已存在";
        case ERR_NO_SPACE:  return "空间不足";
        case ERR_PERMISSION:return "权限不足";
        case ERR_INVALID:   return "无效参数";
        case ERR_NOT_EMPTY: return "目录非空";
        default:            return "未知错误";
    }
}

/**
 * 分割命令行参数
 */
static int split_args(char* cmdline, char** argv, int max_args) {
    int argc = 0;
    char* token = strtok(cmdline, " \t");
    
    while (token != NULL && argc < max_args) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    
    return argc;
}

/**
 * 去除字符串首尾空白
 */
static char* trim(char* str) {
    char* end;
    
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

/**
 * 打印程序启动横幅
 */
void print_banner(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║       多用户FAT文件系统 - 课程设计                            ║\n");
    printf("║       Multi-User FAT File System                              ║\n");
    printf("║                                                               ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║  命令列表:                                                    ║\n");
    printf("║    目录: mkdir, rmdir, cd, ls, pwd                            ║\n");
    printf("║    文件: create, delete, open, close, read, write, seek       ║\n");
    printf("║    用户: login, logout, adduser                               ║\n");
    printf("║    系统: format, save, load, exit                             ║\n");
    printf("║    其他: help, clear                                          ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║  输入 'help' 查看详细帮助                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * 显示帮助信息
 */
void show_help(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("                    多用户FAT文件系统帮助\n");
    printf("═══════════════════════════════════════════════════════════════════\n\n");
    
    printf("【目录操作】\n");
    printf("  mkdir <路径>          - 创建目录\n");
    printf("  rmdir <路径>          - 删除空目录\n");
    printf("  cd <路径>             - 切换当前目录\n");
    printf("  pwd                   - 显示当前目录路径\n");
    printf("  ls [路径]             - 列出目录内容\n\n");
    
    printf("【文件操作】\n");
    printf("  create <路径>         - 创建文件\n");
    printf("  delete <路径>         - 删除文件\n");
    printf("  open <路径> [模式]    - 打开文件 (0只读/1只写/2读写)\n");
    printf("  close <fd>            - 关闭文件\n");
    printf("  read <fd> <字节数>    - 读取文件内容\n");
    printf("  write <fd> <内容>     - 写入文件内容\n");
    printf("  seek <fd> <偏移> [起始] - 定位文件指针\n\n");
    
    printf("【用户操作】\n");
    printf("  login <用户名> <密码> - 登录系统\n");
    printf("  logout                - 登出\n");
    printf("  adduser <用户名> <密码> [admin/user] - 添加用户\n\n");
    
    printf("【系统操作】\n");
    printf("  format                - 格式化磁盘（危险！）\n");
    printf("  save                  - 手动保存文件系统\n");
    printf("  load                  - 重新加载文件系统\n");
    printf("  exit                  - 退出系统\n\n");
    
    printf("【其他】\n");
    printf("  help, ?               - 显示此帮助\n");
    printf("  clear                 - 清屏\n\n");
    
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("【使用示例】\n");
    printf("  login admin admin\n");
    printf("  mkdir /home\n");
    printf("  cd /home\n");
    printf("  create test.txt\n");
    printf("  open test.txt 2\n");
    printf("  write 0 \"Hello World\"\n");
    printf("  read 0 100\n");
    printf("  close 0\n");
    printf("═══════════════════════════════════════════════════════════════════\n\n");
}

/**
 * 显示提示符
 */
void print_prompt(void) {
    if (currentUserID == ID_NOT_LOGIN) {
        printf("[未登录] $ ");
    } else {
        char username[32] = "unknown";
        for (int i = 0; i < MAX_USERS; i++) {
            if (sfat.Users[i].userid == currentUserID && 
                sfat.Users[i].role != ROLE_NULL) {
                strncpy(username, sfat.Users[i].username, 31);
                username[31] = '\0';
                break;
            }
        }
        printf("[%s] $ ", username);
    }
}

/**
 * 执行单条命令
 */
int execute_command(const char* cmd_str) {
    char cmdline[CMD_MAX_LEN];
    char* argv[ARG_MAX_COUNT];
    int argc;
    int ret;
    
    if (cmd_str == NULL || strlen(cmd_str) == 0) {
        return SUCCESS;
    }
    
    strncpy(cmdline, cmd_str, CMD_MAX_LEN - 1);
    cmdline[CMD_MAX_LEN - 1] = '\0';
    
    argc = split_args(cmdline, argv, ARG_MAX_COUNT);
    if (argc == 0) {
        return SUCCESS;
    }

    // ========== 登录检查 ==========
    // 如果未登录，且不是公开命令，则拒绝执行
    if (currentUserID == ID_NOT_LOGIN && !is_public_command(argv[0])) {
        printf("请先登录！使用 'login <用户名> <密码>' 登录\n");
        return ERR_PERMISSION;
    }
    
    // ========== 目录操作 ==========
    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc < 2) {
            printf("用法: mkdir <路径>\n");
            return ERR_INVALID;
        }
        ret = mkdir(argv[1]);
        if (ret == SUCCESS) printf("目录创建成功\n");
        else printf("创建失败: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "rmdir") == 0) {
        if (argc < 2) {
            printf("用法: rmdir <路径>\n");
            return ERR_INVALID;
        }
        ret = rmdir(argv[1]);
        if (ret == SUCCESS) printf("目录删除成功\n");
        else printf("删除失败: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "cd") == 0) {
        const char* path = (argc >= 2) ? argv[1] : "/";
        ret = cd(path);
        if (ret != SUCCESS) {
            printf("切换失败: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "pwd") == 0) {
        char buf[MAX_PATH_LEN];
        char* result = getcwd(buf, sizeof(buf));
        if (result != NULL) {
            printf("%s\n", buf);
        } else {
            printf("获取当前目录失败\n");
        }
        return SUCCESS;
    }
    
    else if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "dir") == 0) {
        const char* path = (argc >= 2) ? argv[1] : NULL;
        ret = dir(path);
        if (ret != SUCCESS) {
            printf("列出目录失败: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    // ========== 文件操作 ==========
    else if (strcmp(argv[0], "create") == 0) {
        if (argc < 2) {
            printf("用法: create <路径>\n");
            return ERR_INVALID;
        }
        ret = create_file(argv[1], currentUserID);
        if (ret == SUCCESS) printf("文件创建成功\n");
        else printf("创建失败: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "delete") == 0 || strcmp(argv[0], "del") == 0) {
        if (argc < 2) {
            printf("用法: delete <路径>\n");
            return ERR_INVALID;
        }
        ret = delete_file(argv[1], currentUserID);
        if (ret == SUCCESS) printf("文件删除成功\n");
        else printf("删除失败: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "open") == 0) {
        if (argc < 2) {
            printf("用法: open <路径> [模式(0只读/1只写/2读写)]\n");
            return ERR_INVALID;
        }
        int mode = (argc >= 3) ? atoi(argv[2]) : 0;
        int fd = open_file(argv[1], mode, currentUserID);
        if (fd >= 0) {
            printf("文件已打开，fd = %d\n", fd);
            return SUCCESS;
        } else {
            printf("打开失败: %s\n", error_to_string(fd));
            return fd;
        }
    }
    
    else if (strcmp(argv[0], "close") == 0) {
        if (argc < 2) {
            printf("用法: close <fd>\n");
            return ERR_INVALID;
        }
        int fd = atoi(argv[1]);
        ret = close_file(fd);
        if (ret == SUCCESS) printf("文件已关闭\n");
        else printf("关闭失败: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "read") == 0) {
        if (argc < 3) {
            printf("用法: read <fd> <字节数>\n");
            return ERR_INVALID;
        }
        int fd = atoi(argv[1]);
        int size = atoi(argv[2]);
        if (size <= 0 || size > 4096) {
            printf("读取字节数无效 (1-4096)\n");
            return ERR_INVALID;
        }
        
        char* buffer = (char*)malloc(size + 1);
        if (buffer == NULL) {
            printf("内存分配失败\n");
            return ERR_NO_SPACE;
        }
        
        int bytes = read_file(fd, buffer, size);
        if (bytes >= 0) {
            buffer[bytes] = '\0';
            printf("读取 %d 字节:\n", bytes);
            printf("----------------------------------------\n");
            printf("%s\n", buffer);
            printf("----------------------------------------\n");
        } else {
            printf("读取失败: %s\n", error_to_string(bytes));
        }
        
        free(buffer);
        return (bytes >= 0) ? SUCCESS : bytes;
    }
    
    else if (strcmp(argv[0], "write") == 0) {
        if (argc < 3) {
            printf("用法: write <fd> <内容>\n");
            return ERR_INVALID;
        }
        
        int fd = atoi(argv[1]);
        
        char content[4096] = {0};
        for (int i = 2; i < argc; i++) {
            if (i > 2) strcat(content, " ");
            strcat(content, argv[i]);
        }
        
        int len = strlen(content);
        int bytes = write_file(fd, content, len);
        
        if (bytes >= 0) {
            printf("写入 %d 字节\n", bytes);
        } else {
            printf("写入失败: %s\n", error_to_string(bytes));
        }
        
        return (bytes >= 0) ? SUCCESS : bytes;
    }
    
    else if (strcmp(argv[0], "seek") == 0) {
        if (argc < 3) {
            printf("用法: seek <fd> <偏移量> [起始位置(0=开头,1=当前,2=结尾)]\n");
            return ERR_INVALID;
        }
        int fd = atoi(argv[1]);
        int offset = atoi(argv[2]);
        int whence = (argc >= 4) ? atoi(argv[3]) : 0;
        
        int newpos = file_seek(fd, offset, whence);
        if (newpos >= 0) {
            printf("当前文件位置: %d\n", newpos);
            return SUCCESS;
        } else {
            printf("定位失败: %s\n", error_to_string(newpos));
            return newpos;
        }
    }
    
    // ========== 用户操作 ==========
    else if (strcmp(argv[0], "login") == 0) {
        if (argc < 3) {
            printf("用法: login <用户名> <密码>\n");
            return ERR_INVALID;
        }
        ret = login(argv[1], argv[2]);
        if (ret != ID_NOT_LOGIN) {
            printf("登录成功！欢迎 %s\n", argv[1]);
            return SUCCESS;
        } else {
            printf("登录失败: %s\n", error_to_string(ret));
            return ret;
        }
    }
    
    else if (strcmp(argv[0], "logout") == 0) {
        logout();
        printf("已登出\n");
        return SUCCESS;
    }
    
/*    else if (strcmp(argv[0], "adduser") == 0) {
        if (argc < 3) {
            printf("用法: adduser <用户名> <密码> [admin/user]\n");
            return ERR_INVALID;
        }
        char role = ROLE_USER;
        if (argc >= 4) {
            if (strcmp(argv[3], "admin") == 0) role = ROLE_ADMIN;
            else if (strcmp(argv[3], "user") == 0) role = ROLE_USER;
            else {
                printf("无效角色，使用 admin 或 user\n");
                return ERR_INVALID;
            }
        }
        ret = addUser(argv[1], argv[2], role);
        if (ret == SUCCESS) {
            printf("用户 %s 创建成功\n", argv[1]);
        } else {
            printf("创建用户失败: %s\n", error_to_string(ret));
        }
        return ret;
    }
*/    
    // ========== 系统操作 ==========
    else if (strcmp(argv[0], "format") == 0) {
        printf("警告：格式化将清除所有数据！确认继续？(yes/no): ");
        char confirm[10];
        if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
            return ERR_INVALID;
        }
        confirm[strcspn(confirm, "\n")] = '\0';
        
        if (strcmp(confirm, "yes") != 0 && strcmp(confirm, "y") != 0) {
            printf("操作已取消\n");
            return SUCCESS;
        }
        
        ret = format();
        if (ret == SUCCESS) {
            printf("格式化成功！\n");
            load();
        } else {
            printf("格式化失败: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "save") == 0) {
        ret = saveToDisk();
        if (ret == SUCCESS) {
            printf("文件系统保存成功\n");
        } else {
            printf("保存失败: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "load") == 0) {
        ret = load();
        if (ret == SUCCESS) {
            printf("文件系统重新加载成功\n");
        } else {
            printf("加载失败: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        printf("正在退出...\n");
        exit(0);
        return SUCCESS;
    }
    
    else if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        show_help();
        return SUCCESS;
    }
    
    else if (strcmp(argv[0], "clear") == 0) {
        printf("\033[2J\033[H");
        return SUCCESS;
    }
    
    else {
        printf("未知命令: '%s'，输入 'help' 查看可用命令\n", argv[0]);
        return ERR_INVALID;
    }
}

/**
 * Shell主循环
 */
void shell_loop(void) {
    char input[CMD_MAX_LEN];
    
    printf("\n欢迎使用多用户FAT文件系统\n");
    printf("提示：首次使用请先 'login' 登录\n\n");
    
    while (1) {
        print_prompt();
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }
        
        input[strcspn(input, "\n")] = '\0';
        
        char* cmd = trim(input);
        if (strlen(cmd) == 0) {
            continue;
        }
        
        execute_command(cmd);
    }
}
