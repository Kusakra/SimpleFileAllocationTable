#include <string.h>
#include "user.h"

static char current_login_user = NOT_LOGIN;

int init_user_system(void) {
    // User 结构体在 SFAT.h 中定义，已在 sfat.Users 数组里
    // 这里初始化第一个管理员用户
    if (sfat.Users[0].role == ROLE_NULL) {
        strncpy(sfat.Users[0].username, "admin", 14 - 1);
        strncpy(sfat.Users[0].password, "admin", 14 - 1);
        sfat.Users[0].role = ROLE_ADMIN;
        sfat.Users[0].userid = 0;
    }
    current_login_user = NOT_LOGIN;
    return 0;
}

int login(const char *username, const char *password) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (sfat.Users[i].role != ROLE_NULL &&
            strcmp(sfat.Users[i].username, username) == 0 &&
            strcmp(sfat.Users[i].password, password) == 0) {
            current_login_user = sfat.Users[i].userid;
            currentUserID = current_login_user;
            return current_login_user;
        }
    }
    return NOT_LOGIN;
}

void logout(void) {
    current_login_user = NOT_LOGIN;
    currentUserID = NOT_LOGIN;
}

char current_user_id(void) {
    return current_login_user;
}

int check_permission(char user_id, const char *path, int perm) {
    if (user_id == NOT_LOGIN) return 0;
    if (user_id < 0 || user_id >= MAX_USERS) return 0;
    
    User *user = &sfat.Users[user_id];
    if (user->role == ROLE_NULL) return 0;
    
    if (user->role == ROLE_ADMIN) return 1;
    
    return (user->role & perm) == perm;
}