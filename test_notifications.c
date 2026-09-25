#define main console_ui_main
#include "src/main.c"
#undef main

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

void test_add_notification_basic() {
    notification_count = 0;
    unread_notifications = 0;

    AddNotification("Test 1");
    if (notification_count == 1 && strcmp(notifications[0].message, "Test 1") == 0) {
        printf("PASS: test_add_notification_basic\n");
    } else {
        printf("FAIL: test_add_notification_basic\n");
        exit(1);
    }
}

void test_add_notification_limit() {
    notification_count = 0;
    unread_notifications = 0;

    int added = MAX_NOTIFICATIONS + 5;
    for (int i = 0; i < added; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Test %d", i);
        AddNotification(msg);
    }

    char expected_first[32];
    snprintf(expected_first, sizeof(expected_first), "Test %d", added - MAX_NOTIFICATIONS);

    char expected_last[32];
    snprintf(expected_last, sizeof(expected_last), "Test %d", added - 1);

    if (notification_count == MAX_NOTIFICATIONS &&
        strcmp(notifications[0].message, expected_first) == 0 &&
        strcmp(notifications[MAX_NOTIFICATIONS-1].message, expected_last) == 0) {
        printf("PASS: test_add_notification_limit\n");
    } else {
        printf("FAIL: test_add_notification_limit (count=%d, first=%s, last=%s)\n",
               notification_count, notifications[0].message, notifications[MAX_NOTIFICATIONS-1].message);
        exit(1);
    }
}

void test_add_notification_long_string() {
    notification_count = 0;

    char long_msg[256];
    memset(long_msg, 'A', 255);
    long_msg[255] = '\0';

    AddNotification(long_msg);

    if (notification_count == 1 && strlen(notifications[0].message) == 127) {
        printf("PASS: test_add_notification_long_string\n");
    } else {
        printf("FAIL: test_add_notification_long_string (len=%zu)\n", strlen(notifications[0].message));
        exit(1);
    }
}

void* thread_func(void* arg) {
    int thread_id = *(int*)arg;
    for (int i = 0; i < 100; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "T%d_%d", thread_id, i);
        AddNotification(msg);
    }
    return NULL;
}

void test_add_notification_thread_safety() {
    notification_count = 0;
    unread_notifications = 0;

    pthread_t threads[4];
    int thread_ids[4] = {1, 2, 3, 4};

    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, thread_func, &thread_ids[i]);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    if (notification_count == MAX_NOTIFICATIONS && unread_notifications == 400) {
        printf("PASS: test_add_notification_thread_safety\n");
    } else {
        printf("FAIL: test_add_notification_thread_safety (count=%d, unread=%d)\n",
               notification_count, unread_notifications);
        exit(1);
    }
}

int main() {
    test_add_notification_basic();
    test_add_notification_limit();
    test_add_notification_long_string();
    test_add_notification_thread_safety();
    printf("All test_notifications tests passed!\n");
    return 0;
}
