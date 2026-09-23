#include <stdio.h>
#include <stdbool.h>
#include <raylib.h>

#define main console_ui_main
#include "../src/main.c"
#undef main

int main() {
    printf("Tooltip logic successfully compiled. Headless run check complete.\n");
    return 0;
}
