#ifndef USER_H
#define USER_H

#include <stdio.h>
#include "SFAT.h"

// 权限标志
#define PERM_READ READ
#define PERM_WRITE WRITE
#define PERM_EXEC EXEC
#define PERM_ADMIN ROLE_ADMIN

int init_user_system(void);
int login(const char *username, const char *password);
void logout(void);
char current_user_id(void);
int check_permission(char user_id, const char *path, int perm);

#endif // USER_H