#define main console_ui_main
#include "src/main.c"
#undef main

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void test_get_config_dir_with_home() {
    setenv("HOME", "/home/testuser", 1);
    char path[256];
    GetConfigDir(path, sizeof(path));
    if (strcmp(path, "/home/testuser/.config/console-ui") == 0) {
        printf("PASS: test_get_config_dir_with_home\n");
    } else {
        printf("FAIL: test_get_config_dir_with_home (got %s)\n", path);
        exit(1);
    }
}

void test_get_config_dir_without_home() {
    unsetenv("HOME");
    char path[256];
    GetConfigDir(path, sizeof(path));
    if (strcmp(path, "/tmp/console-ui") == 0) {
        printf("PASS: test_get_config_dir_without_home\n");
    } else {
        printf("FAIL: test_get_config_dir_without_home (got %s)\n", path);
        exit(1);
    }
}

int main() {
    test_get_config_dir_with_home();
    test_get_config_dir_without_home();
    printf("All test_config_dir tests passed!\n");
    return 0;
}
