
// 文件系统属性
#define CLUSTER_SIZE 4096 // 簇大小（4KB）
#define MAX_CLUSTERS 16384 // 最大簇数（共64MB）
#define DIRENTRY_SIZE 64 // 目录项大小（64字节）
#define USER_SIZE 32 // 用户结构体大小（32字节）

#define MAX_ROOT_FILES (ROOT_DIR_CLUSTERS * (CLUSTER_SIZE / DIRENTRY_SIZE)) // 根目录最大文件数
#define MAX_FILENAME_LENGTH 16 // 文件名最大长度
#define MAX_EXTENSION_LENGTH 4 // 文件扩展名最大长度
#define MAX_OPEN_FILES 64 // 系统同时打开文件的最大数量
#define MAX_USERS 128 // 最大用户数
#define MAX_STACK_DEPTH 128 // 最大目录栈深度（0-127）

// 用户角色定义
#define ROLE_NULL 0x00 // 空，代表该项未使用
#define ROLE_ADMIN 0x48 // 管理员
#define ROLE_USER 0x52 // 普通用户

// 用户ID定义
#define NOT_LOGIN 0x44  // 未登录

// 文件名首字符定义
#define UNUSED 0x00 // 未使用
#define DELETED 0xE5 // 已删除
// 文件类型
#define SUBDIR 0x10 // 子目录
#define ARCHIVE 0x20 // 文件
// 文件权限
#define READ 0x04   // 读权限
#define WRITE 0x02  // 写权限
#define EXEC 0x01   // 执行权限
// 文件修改标志
#define UNMODIFIED 0x00 // 文件未修改
#define MODIFIED 0x01   // 文件已修改

// FAT表项定义
#define FAT_FREE 0x00000000 // 簇空闲
#define FAT_EOF 0xFFFFFFFF // 簇链结束标志

// 磁盘布局
#define USER_TABLE_CLUSTER 3 // 用户表簇号
#define RESERVE_CLUSTERS 4 // 保留簇数

#define FAT_START_CLUSTER RESERVE_CLUSTERS // FAT表起始簇号
#define FAT_CLUSTERS 16 // FAT表占用簇数

#define ROOT_DIR_START_CLUSTER (RESERVE_CLUSTERS + FAT_CLUSTERS) // 根目录起始簇号
#define ROOT_DIR_CLUSTERS 4 // 根目录占用簇数

#define DATA_START_CLUSTER (RESERVE_CLUSTERS + FAT_CLUSTERS + ROOT_DIR_CLUSTERS) // 数据区起始簇号

#define LOG_NULL 0x00 // 日志关闭
#define LOG_INFO 0x14 // 日志信息标志
#define LOG_WARNING 0x50 // 日志调试标志
#define LOG_ERROR 0x26 // 日志错误标志


// 目录项（64字节）
typedef struct DirEntry {
    char name[16]; // 文件或目录名称
        // 首字节定义
        // 0x00	未使用
        // 0xE5	已删除
    char extension[4]; // 文件扩展名（如果是目录则为空）
    char type; // 文件类型
        // 0x10	子目录
        // 0x20	文件
        
    char owner; // 文件所有者的用户ID
    char permission; // 文件权限--rwx

    char createdTime[4];    // 创建时间
    char modifiedTime[4];   // 最后修改时间
    char accessedTime[4];   // 最后访问时间

    unsigned int size;      // 文件大小，若为目录则为目录文件数
    unsigned int startCluster; // 文件数据的起始簇号
} DirEntry;

// 目录
typedef struct Directory {
    DirEntry *entries;  // 目录项数组
    int count; // 当前目录中的条目数量
} Directory;

// 用户（32字节）
typedef struct User {
    char username[14]; // 用户名
    char password[14]; // 密码
    char role; // 角色
    char userid;        // 用户ID，无特殊含义
} User;

// 系统打开文件结构
typedef struct OpenFile {
    char modify_flag; // 修改标志
    DirEntry *entry; // 打开的文件的目录项指针
    unsigned int currentCluster; // 当前访问的簇号
    unsigned int offset; // 当前访问的偏移量
    unsigned int count;  // 文件被使用计数
} OpenFile;

typedef struct SFAT {
    /* 
        尽管fat表共16384项，但0-23簇被系统占用，数据区从第24簇开始，因此索引0-23的FAT项
        不应被使用，最低应从24开始使用
    */
    unsigned int *fat; // FAT表指针，指向fat数组（占用16簇，每簇4096字节，共16384项）
    unsigned int nextFreeCluster; // 下一个空闲簇号
    unsigned int freeClusterCount; // 空闲簇数量
    Directory rootDirectory;        // 根目录
    Directory dirStack[MAX_STACK_DEPTH]; // 目录栈
    OpenFile openFiles[MAX_OPEN_FILES]; // 打开文件表
    User Users[MAX_USERS]; // 用户列表
    FILE *fd; // 磁盘文件指针
} SFAT;

extern SFAT sfat;
extern char currentUserID;
extern unsigned short cdi;
extern OpenFile NULL_FILE;
extern User NULL_USER;
extern int format();
extern int load();
extern int init();
extern char *readCluster(unsigned int cluster, unsigned int n);
extern int writeCluster(const void *buf, unsigned int cluster, unsigned int n);
extern int saveToDisk();
extern void logger(const char *message, char level);
extern Directory* dirFromDisk(unsigned int cluster);