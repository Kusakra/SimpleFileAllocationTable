#include <string.h>
#include "user.h"
void userMenu(void);
static char current_login_user = NOT_LOGIN;

int init_user_system(void) {
    // 强制初始化第一个管理员用户
    strncpy(sfat.Users[0].username, "admin", 14 - 1);
    strncpy(sfat.Users[0].password, "admin", 14 - 1);
    sfat.Users[0].role = ROLE_ADMIN;
    sfat.Users[0].userid = 0;
    
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

void userMenu(void) {
    int choice;
    char username[15];
    char password[15];
    int result;

    while (1) {
        printf("\n===== USER MENU =====\n");
        printf("1. Login\n");
        printf("2. Logout\n");
        printf("3. Show current user\n");
        printf("0. Back to main menu\n");
        printf("====================\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        getchar();  // consume newline

        switch (choice) {
            case 1:
                printf("Enter username: ");
                fgets(username, sizeof(username), stdin);
                username[strcspn(username, "\n")] = '\0';
                printf("Enter password: ");
                fgets(password, sizeof(password), stdin);
                password[strcspn(password, "\n")] = '\0';
                result = login(username, password);
                if (result != NOT_LOGIN) {
                    printf("[INFO] Login successful! User ID: %d\n", result);
                } else {
                    printf("[ERROR] Login failed. Invalid username or password.\n");
                }
                break;

            case 2:
                if (current_user_id() != NOT_LOGIN) {
                    logout();
                    printf("[INFO] Logout successful.\n");
                } else {
                    printf("[ERROR] No user logged in.\n");
                }
                break;

            case 3:
                if (current_user_id() != NOT_LOGIN) {
                    printf("[INFO] Current user ID: %d\n", current_user_id());
                } else {
                    printf("[INFO] No user logged in.\n");
                }
                break;

            case 0:
                return;

            default:
                printf("[ERROR] Invalid choice!\n");
        }
    }
}