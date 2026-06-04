/**
 * shell.c - �����н���������Ա4��д��
 * 
 * ���ܣ������û���������������Ӧ���ļ�ϵͳ�ӿ�
 */

#include "SFAT.h"
#include "shell.h"
#include "user.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ������󳤶�
#define CMD_MAX_LEN     512
#define ARG_MAX_COUNT   16

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 256
#endif

// �����붨�壨���SFAT.h��û�У����ﲹ�䣩
#ifndef SUCCESS
#define SUCCESS         0
#define ERR_NOT_FOUND   -1
#define ERR_EXIST       -2
#define ERR_NO_SPACE    -3
#define ERR_PERMISSION  -4
#define ERR_INVALID     -5
#define ERR_NOT_EMPTY   -6
#endif

// �ļ�ģʽת����shell���� -> FileMode��
#define FILE_MODE_READ      0
#define FILE_MODE_WRITE     1
#define FILE_MODE_APPEND    2

// ������Ա2�ĺ������»���������
extern int cd(const char *path);
extern int dir(const char *path);
extern int mkdir(const char *name);
extern int rmdir(const char *name);
extern char* getcwd(char* buf, int size);

// ������Ա3�ĺ������»���������
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
 * ��������Ƿ������¼����ִ��
 */
static int is_public_command(const char *cmd) {
    if (cmd == NULL) return 0;
    
    // ���������¼�������б�
    if (strcmp(cmd, "login") == 0) return 1;
    if (strcmp(cmd, "help") == 0) return 1;
    if (strcmp(cmd, "?") == 0) return 1;
    if (strcmp(cmd, "exit") == 0) return 1;
    if (strcmp(cmd, "quit") == 0) return 1;
    if (strcmp(cmd, "clear") == 0) return 1;
    
    return 0;
}

/**
 * ������Ϣӳ��
 */
static const char* error_to_string(int err) {
    switch (err) {
        case SUCCESS:       return "�ɹ�";
        case ERR_NOT_FOUND: return "�ļ���Ŀ¼������";
        case ERR_EXIST:     return "�ļ���Ŀ¼�Ѵ���";
        case ERR_NO_SPACE:  return "�ռ䲻��";
        case ERR_PERMISSION:return "Ȩ�޲���";
        case ERR_INVALID:   return "��Ч����";
        case ERR_NOT_EMPTY: return "Ŀ¼�ǿ�";
        default:            return "δ֪����";
    }
}

/**
 * �ָ������в���
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
 * ȥ���ַ�����β�հ�
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
 * ��ӡ�����������
 */
void print_banner(void) {
    printf("\n");
    printf("�X�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�[\n");
    printf("�U                                                               �U\n");
    printf("�U       ���û�FAT�ļ�ϵͳ - �γ����                            �U\n");
    printf("�U       Multi-User FAT File System                              �U\n");
    printf("�U                                                               �U\n");
    printf("�d�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�g\n");
    printf("�U  �����б�:                                                    �U\n");
    printf("�U    Ŀ¼: mkdir, rmdir, cd, ls, pwd                            �U\n");
    printf("�U    �ļ�: create, delete, open, close, read, write, seek       �U\n");
    printf("�U    �û�: login, logout, adduser                               �U\n");
    printf("�U    ϵͳ: format, save, load, exit                             �U\n");
    printf("�U    ����: help, clear                                          �U\n");
    printf("�d�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�g\n");
    printf("�U  ���� 'help' �鿴��ϸ����                                     �U\n");
    printf("�^�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�a\n");
    printf("\n");
}

/**
 * ��ʾ������Ϣ
 */
void show_help(void) {
    printf("\n");
    printf("�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T\n");
    printf("                    ���û�FAT�ļ�ϵͳ����\n");
    printf("�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T\n\n");
    
    printf("��Ŀ¼������\n");
    printf("  mkdir <·��>          - ����Ŀ¼\n");
    printf("  rmdir <·��>          - ɾ����Ŀ¼\n");
    printf("  cd <·��>             - �л���ǰĿ¼\n");
    printf("  pwd                   - ��ʾ��ǰĿ¼·��\n");
    printf("  ls [·��]             - �г�Ŀ¼����\n\n");
    
    printf("���ļ�������\n");
    printf("  create <·��>         - �����ļ�\n");
    printf("  delete <·��>         - ɾ���ļ�\n");
    printf("  open <·��> [ģʽ]    - ���ļ� (0ֻ��/1ֻд/2��д)\n");
    printf("  close <fd>            - �ر��ļ�\n");
    printf("  read <fd> <�ֽ���>    - ��ȡ�ļ�����\n");
    printf("  write <fd> <����>     - д���ļ�����\n");
    printf("  seek <fd> <ƫ��> [��ʼ] - ��λ�ļ�ָ��\n\n");
    
    printf("���û�������\n");
    printf("  login <�û���> <����> - ��¼ϵͳ\n");
    printf("  logout                - �ǳ�\n");
    printf("  adduser <�û���> <����> [admin/user] - �����û�\n\n");
    
    printf("��ϵͳ������\n");
    printf("  format                - ��ʽ�����̣�Σ�գ���\n");
    printf("  save                  - �ֶ������ļ�ϵͳ\n");
    printf("  load                  - ���¼����ļ�ϵͳ\n");
    printf("  exit                  - �˳�ϵͳ\n\n");
    
    printf("��������\n");
    printf("  help, ?               - ��ʾ�˰���\n");
    printf("  clear                 - ����\n\n");
    
    printf("�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T\n");
    printf("��ʹ��ʾ����\n");
    printf("  login admin admin\n");
    printf("  mkdir /home\n");
    printf("  cd /home\n");
    printf("  create test.txt\n");
    printf("  open test.txt 2\n");
    printf("  write 0 \"Hello World\"\n");
    printf("  read 0 100\n");
    printf("  close 0\n");
    printf("�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T\n\n");
}

/**
 * ��ʾ��ʾ��
 */
void print_prompt(void) {
    if (currentUserID == ID_NOT_LOGIN) {
        printf("[δ��¼] $ ");
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
 * ִ�е�������
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

    // ========== ��¼��� ==========
    // ���δ��¼���Ҳ��ǹ��������ܾ�ִ��
    if (currentUserID == ID_NOT_LOGIN && !is_public_command(argv[0])) {
        printf("���ȵ�¼��ʹ�� 'login <�û���> <����>' ��¼\n");
        return ERR_PERMISSION;
    }
    
    // ========== Ŀ¼���� ==========
    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc < 2) {
            printf("�÷�: mkdir <·��>\n");
            return ERR_INVALID;
        }
        ret = mkdir(argv[1]);
        if (ret == SUCCESS) printf("Ŀ¼�����ɹ�\n");
        else printf("����ʧ��: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "rmdir") == 0) {
        if (argc < 2) {
            printf("�÷�: rmdir <·��>\n");
            return ERR_INVALID;
        }
        ret = rmdir(argv[1]);
        if (ret == SUCCESS) printf("Ŀ¼ɾ���ɹ�\n");
        else printf("ɾ��ʧ��: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "cd") == 0) {
        const char* path = (argc >= 2) ? argv[1] : "/";
        ret = cd(path);
        if (ret != SUCCESS) {
            printf("�л�ʧ��: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "pwd") == 0) {
        // 彻底丢弃宿主机的 getcwd，改用我们 SFAT 自己的路径打印
        pwd(); 
        
        return SUCCESS;
    }
    
    else if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "dir") == 0) {
        const char* path = (argc >= 2) ? argv[1] : NULL;
        ret = dir(path);
        if (ret != SUCCESS) {
            printf("�г�Ŀ¼ʧ��: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    // ========== �ļ����� ==========
    else if (strcmp(argv[0], "create") == 0) {
        if (argc < 2) {
            printf("�÷�: create <·��>\n");
            return ERR_INVALID;
        }
        ret = create_file(argv[1], currentUserID);
        if (ret == SUCCESS) printf("�ļ������ɹ�\n");
        else printf("����ʧ��: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "delete") == 0 || strcmp(argv[0], "del") == 0) {
        if (argc < 2) {
            printf("�÷�: delete <·��>\n");
            return ERR_INVALID;
        }
        ret = delete_file(argv[1], currentUserID);
        if (ret == SUCCESS) printf("�ļ�ɾ���ɹ�\n");
        else printf("ɾ��ʧ��: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "open") == 0) {
        if (argc < 2) {
            printf("�÷�: open <·��> [ģʽ(0ֻ��/1ֻд/2��д)]\n");
            return ERR_INVALID;
        }
        int mode = (argc >= 3) ? atoi(argv[2]) : 0;
        int fd = open_file(argv[1], mode, currentUserID);
        if (fd >= 0) {
            printf("�ļ��Ѵ򿪣�fd = %d\n", fd);
            return SUCCESS;
        } else {
            printf("��ʧ��: %s\n", error_to_string(fd));
            return fd;
        }
    }
    
    else if (strcmp(argv[0], "close") == 0) {
        if (argc < 2) {
            printf("�÷�: close <fd>\n");
            return ERR_INVALID;
        }
        int fd = atoi(argv[1]);
        ret = close_file(fd);
        if (ret == SUCCESS) printf("�ļ��ѹر�\n");
        else printf("�ر�ʧ��: %s\n", error_to_string(ret));
        return ret;
    }
    
    else if (strcmp(argv[0], "read") == 0) {
        if (argc < 3) {
            printf("�÷�: read <fd> <�ֽ���>\n");
            return ERR_INVALID;
        }
        int fd = atoi(argv[1]);
        int size = atoi(argv[2]);
        if (size <= 0 || size > 4096) {
            printf("��ȡ�ֽ�����Ч (1-4096)\n");
            return ERR_INVALID;
        }
        
        char* buffer = (char*)malloc(size + 1);
        if (buffer == NULL) {
            printf("�ڴ����ʧ��\n");
            return ERR_NO_SPACE;
        }
        
        int bytes = read_file(fd, buffer, size);
        if (bytes >= 0) {
            buffer[bytes] = '\0';
            printf("��ȡ %d �ֽ�:\n", bytes);
            printf("----------------------------------------\n");
            printf("%s\n", buffer);
            printf("----------------------------------------\n");
        } else {
            printf("��ȡʧ��: %s\n", error_to_string(bytes));
        }
        
        free(buffer);
        return (bytes >= 0) ? SUCCESS : bytes;
    }
    
    else if (strcmp(argv[0], "write") == 0) {
        if (argc < 3) {
            printf("�÷�: write <fd> <����>\n");
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
            printf("д�� %d �ֽ�\n", bytes);
        } else {
            printf("д��ʧ��: %s\n", error_to_string(bytes));
        }
        
        return (bytes >= 0) ? SUCCESS : bytes;
    }
    
    else if (strcmp(argv[0], "seek") == 0) {
        if (argc < 3) {
            printf("�÷�: seek <fd> <ƫ����> [��ʼλ��(0=��ͷ,1=��ǰ,2=��β)]\n");
            return ERR_INVALID;
        }
        int fd = atoi(argv[1]);
        int offset = atoi(argv[2]);
        int whence = (argc >= 4) ? atoi(argv[3]) : 0;
        
        int newpos = file_seek(fd, offset, whence);
        if (newpos >= 0) {
            printf("��ǰ�ļ�λ��: %d\n", newpos);
            return SUCCESS;
        } else {
            printf("��λʧ��: %s\n", error_to_string(newpos));
            return newpos;
        }
    }
    
    // ========== �û����� ==========
    else if (strcmp(argv[0], "login") == 0) {
        if (argc < 3) {
            printf("�÷�: login <�û���> <����>\n");
            return ERR_INVALID;
        }
        ret = login(argv[1], argv[2]);
        if (ret != ID_NOT_LOGIN) {
            printf("��¼�ɹ�����ӭ %s\n", argv[1]);
            return SUCCESS;
        } else {
            printf("��¼ʧ��: %s\n", error_to_string(ret));
            return ret;
        }
    }
    
    else if (strcmp(argv[0], "logout") == 0) {
        logout();
        printf("�ѵǳ�\n");
        return SUCCESS;
    }
    
/*    else if (strcmp(argv[0], "adduser") == 0) {
        if (argc < 3) {
            printf("�÷�: adduser <�û���> <����> [admin/user]\n");
            return ERR_INVALID;
        }
        char role = ROLE_USER;
        if (argc >= 4) {
            if (strcmp(argv[3], "admin") == 0) role = ROLE_ADMIN;
            else if (strcmp(argv[3], "user") == 0) role = ROLE_USER;
            else {
                printf("��Ч��ɫ��ʹ�� admin �� user\n");
                return ERR_INVALID;
            }
        }
        ret = addUser(argv[1], argv[2], role);
        if (ret == SUCCESS) {
            printf("�û� %s �����ɹ�\n", argv[1]);
        } else {
            printf("�����û�ʧ��: %s\n", error_to_string(ret));
        }
        return ret;
    }
*/    
    // ========== ϵͳ���� ==========
    else if (strcmp(argv[0], "format") == 0) {
        printf("���棺��ʽ��������������ݣ�ȷ�ϼ�����(yes/no): ");
        char confirm[10];
        if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
            return ERR_INVALID;
        }
        confirm[strcspn(confirm, "\n")] = '\0';
        
        if (strcmp(confirm, "yes") != 0 && strcmp(confirm, "y") != 0) {
            printf("������ȡ��\n");
            return SUCCESS;
        }
        
        ret = format();
        if (ret == SUCCESS) {
            printf("��ʽ���ɹ���\n");
            load();
        } else {
            printf("��ʽ��ʧ��: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "save") == 0) {
        ret = saveToDisk();
        if (ret == SUCCESS) {
            printf("�ļ�ϵͳ����ɹ�\n");
        } else {
            printf("����ʧ��: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "load") == 0) {
        ret = load();
        if (ret == SUCCESS) {
            printf("�ļ�ϵͳ���¼��سɹ�\n");
        } else {
            printf("����ʧ��: %s\n", error_to_string(ret));
        }
        return ret;
    }
    
    else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        printf("�����˳�...\n");
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
        printf("δ֪����: '%s'������ 'help' �鿴��������\n", argv[0]);
        return ERR_INVALID;
    }
}

/**
 * Shell��ѭ��
 */
void shell_loop(void) {
    char input[CMD_MAX_LEN];
    
    printf("\n��ӭʹ�ö��û�FAT�ļ�ϵͳ\n");
    printf("��ʾ���״�ʹ������ 'login' ��¼\n\n");
    
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
