/**
 * shell.h - Shell模块头文件（成员4编写）
 */

#ifndef SHELL_H
#define SHELL_H

// Shell主循环
extern void shell_loop(void);

// 执行单条命令
extern int execute_command(const char* cmd);

// 显示帮助
extern void show_help(void);

// 显示提示符
extern void print_prompt(void);

// 显示欢迎横幅
extern void print_banner(void);
#endif
