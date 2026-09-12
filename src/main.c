#define _POSIX_C_SOURCE 200809L
#include <raylib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#if defined(RAYLIB_VERSION_MAJOR) && (RAYLIB_VERSION_MAJOR < 5 || (RAYLIB_VERSION_MAJOR == 5 && RAYLIB_VERSION_MINOR < 5))
// Fallback for Raylib <= 5.0 test environments
void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments, float lineThick, Color color) {
    DrawRectangleRoundedLines(rec, roundness, segments, lineThick, color);
}
#endif

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

// Palette
static const Color COLOR_BG = { 13, 17, 23, 255 };
static const Color COLOR_CARD_IDLE = { 22, 27, 34, 180 };
static const Color COLOR_CARD_FOCUS = { 33, 40, 50, 220 };
static const Color COLOR_ACCENT = { 0, 242, 254, 255 };
static const Color COLOR_TEXT_MAIN = { 240, 240, 240, 255 };
static const Color COLOR_TEXT_MUTED = { 150, 150, 150, 255 };
static const Color COLOR_TOPBAR = { 13, 17, 23, 220 };
static const Color COLOR_ERROR = { 255, 80, 80, 255 };

#define MENU_ITEM_COUNT 4
static const char* menu_items[MENU_ITEM_COUNT] = {
    "Library / Games",
    "Media Deck",
    "Terminal Tools",
    "System & Settings"
};

typedef enum {
    STATE_DASHBOARD,
    STATE_UPDATING,
    STATE_SETTINGS
} AppUIState;

AppUIState current_state = STATE_DASHBOARD;

typedef enum {
    PROFILE_AUTO = 0,
    PROFILE_XBOX,
    PROFILE_PS5,
    PROFILE_PS2_LEGACY
} ControllerProfile;

ControllerProfile active_profile = PROFILE_AUTO;

bool update_available = false;
pthread_mutex_t update_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_NOTIFICATIONS 32
char notifications[MAX_NOTIFICATIONS][128];
int notification_count = 0;
int unread_notifications = 0;
pthread_mutex_t notif_mutex = PTHREAD_MUTEX_INITIALIZER;
bool notify_sound_pending = false;

bool show_notifications = false;
float notif_drawer_x = SCREEN_WIDTH;

bool update_in_progress = false;
bool update_success = false;
bool update_failed = false;
char update_status_text[256] = "Initializing...";

bool bgm_muted = false;

void AddNotification(const char* msg) {
    pthread_mutex_lock(&notif_mutex);
    if (notification_count < MAX_NOTIFICATIONS) {
        strncpy(notifications[notification_count], msg, 127);
        notifications[notification_count][127] = '\0';
        notification_count++;
    } else {
        for (int i = 1; i < MAX_NOTIFICATIONS; i++) {
            strcpy(notifications[i-1], notifications[i]);
        }
        strncpy(notifications[MAX_NOTIFICATIONS-1], msg, 127);
        notifications[MAX_NOTIFICATIONS-1][127] = '\0';
    }
    unread_notifications++;
    notify_sound_pending = true;
    pthread_mutex_unlock(&notif_mutex);
}

void* UpdateCheckerThread(void* arg) {
    (void)arg;
    while (1) {
        FILE* fp = popen("git fetch origin main && git rev-list HEAD..origin/main --count", "r");
        if (fp != NULL) {
            char buffer[64];
            if (fgets(buffer, sizeof(buffer), fp) != NULL) {
                int count = atoi(buffer);
                if (count > 0) {
                    pthread_mutex_lock(&update_mutex);
                    if (!update_available) {
                        update_available = true;
                        pthread_mutex_unlock(&update_mutex);
                        AddNotification("System Update Available");
                        pthread_mutex_lock(&update_mutex);
                    }
                    pthread_mutex_unlock(&update_mutex);
                }
            }
            pclose(fp);
        }

        for(int i=0; i<300; i++) {
            pthread_mutex_lock(&update_mutex);
            AppUIState s = current_state;
            pthread_mutex_unlock(&update_mutex);

            // Wait out the updating state if it happens
            while (s == STATE_UPDATING) {
                sleep(1);
                pthread_mutex_lock(&update_mutex);
                s = current_state;
                pthread_mutex_unlock(&update_mutex);
            }
            sleep(1);
        }
    }
    return NULL;
}

void* UpdateInstallerThread(void* arg) {
    (void)arg;

    pthread_mutex_lock(&update_mutex);
    strcpy(update_status_text, "Fetching repository...");
    pthread_mutex_unlock(&update_mutex);

    int ret = system("git pull origin main > update.log 2>&1");
    if (ret != 0) {
        pthread_mutex_lock(&update_mutex);
        update_failed = true;
        strcpy(update_status_text, "Failed to pull from repository.");
        pthread_mutex_unlock(&update_mutex);
        return NULL;
    }

    pthread_mutex_lock(&update_mutex);
    strcpy(update_status_text, "Compiling targets...");
    pthread_mutex_unlock(&update_mutex);

    ret = system("cmake -B build -DCMAKE_BUILD_TYPE=Release >> update.log 2>&1 && cmake --build build -j$(nproc) >> update.log 2>&1");
    if (ret != 0) {
        pthread_mutex_lock(&update_mutex);
        update_failed = true;
        strcpy(update_status_text, "Compilation failed! Check update.log");
        pthread_mutex_unlock(&update_mutex);
        return NULL;
    }

    pthread_mutex_lock(&update_mutex);
    strcpy(update_status_text, "Finalizing assets...");
    sleep(1); // Give it a brief moment to show success
    update_success = true;
    pthread_mutex_unlock(&update_mutex);

    return NULL;
}

int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Console UI");
    SetTargetFPS(60);
    ToggleFullscreen();
    InitAudioDevice();

    Sound sound_nav = {0};
    if (FileExists("assets/sounds/hover.ogg")) sound_nav = LoadSound("assets/sounds/hover.ogg");
    else if (FileExists("assets/sounds/nav.wav")) sound_nav = LoadSound("assets/sounds/nav.wav");

    Sound sound_select = {0};
    if (FileExists("assets/sounds/click.ogg")) sound_select = LoadSound("assets/sounds/click.ogg");
    else if (FileExists("assets/sounds/select.wav")) sound_select = LoadSound("assets/sounds/select.wav");

    Sound sound_back = {0};
    if (FileExists("assets/sounds/back.wav")) sound_back = LoadSound("assets/sounds/back.wav");
    else if (FileExists("assets/sounds/click.ogg")) sound_back = LoadSound("assets/sounds/click.ogg");

    Sound sound_notify = {0};
    if (FileExists("assets/sounds/notify.wav")) sound_notify = LoadSound("assets/sounds/notify.wav");
    else if (FileExists("assets/sounds/click.ogg")) sound_notify = LoadSound("assets/sounds/click.ogg");

    Music bgm = {0};
    if (FileExists("assets/sounds/background.wav")) bgm = LoadMusicStream("assets/sounds/background.wav");
    else if (FileExists("assets/sounds/bgm.ogg")) bgm = LoadMusicStream("assets/sounds/bgm.ogg");
    else if (FileExists("assets/sounds/bgm.wav")) bgm = LoadMusicStream("assets/sounds/bgm.wav");
    if (bgm.stream.buffer != NULL) {
        SetMusicVolume(bgm, 0.2f);
        PlayMusicStream(bgm);
    }

    Texture2D tex_icons[MENU_ITEM_COUNT] = {0};
    const char* image_paths[MENU_ITEM_COUNT] = {
        "assets/images/games.png",
        "assets/images/media.png",
        "assets/images/terminal.png",
        "assets/images/settings.png"
    };
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (FileExists(image_paths[i])) {
            tex_icons[i] = LoadTexture(image_paths[i]);
        }
    }

    SetExitKey(KEY_NULL);

    int current_selection = 0;
    bool should_close = false;

    float card_scales[MENU_ITEM_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float card_y_offsets[MENU_ITEM_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float spinner_angle = 0.0f;

    pthread_t checker_thread;
    pthread_create(&checker_thread, NULL, UpdateCheckerThread, NULL);

    while (!WindowShouldClose() && !should_close) {
        if (bgm.stream.buffer != NULL) UpdateMusicStream(bgm);

        float dt = GetFrameTime();

        pthread_mutex_lock(&notif_mutex);
        if (notify_sound_pending) {
            if (sound_notify.stream.buffer != NULL) PlaySound(sound_notify);
            notify_sound_pending = false;
        }
        pthread_mutex_unlock(&notif_mutex);

        pthread_mutex_lock(&update_mutex);
        AppUIState state_copy = current_state;
        pthread_mutex_unlock(&update_mutex);

        if (state_copy == STATE_DASHBOARD) {
            bool move_left = IsKeyPressed(KEY_LEFT);
            bool move_right = IsKeyPressed(KEY_RIGHT);
            bool select = IsKeyPressed(KEY_ENTER);
            bool back = IsKeyPressed(KEY_ESCAPE);
            bool toggle_notif = IsKeyPressed(KEY_N) || IsKeyPressed(KEY_X);
            bool trigger_update = IsKeyPressed(KEY_U) || IsKeyPressed(KEY_Y);

            if (IsGamepadAvailable(0)) {
                ControllerProfile effective_profile = active_profile;
                if (effective_profile == PROFILE_AUTO) {
                    const char* gp_name = GetGamepadName(0);
                    if (gp_name != NULL && (strstr(gp_name, "Sony") != NULL || strstr(gp_name, "DualSense") != NULL || strstr(gp_name, "PS5") != NULL)) {
                        effective_profile = PROFILE_PS5;
                    } else {
                        effective_profile = PROFILE_XBOX;
                    }
                }

                static double last_nav_time = 0;
                double current_time = GetTime();

                float axis_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
                if (fabs(axis_x) < 0.25f) axis_x = 0.0f; // Deadzone

                if (effective_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.5f && (current_time - last_nav_time > 0.3))) {
                        move_left = true;
                        if (axis_x < -0.5f) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.5f && (current_time - last_nav_time > 0.3))) {
                        move_right = true;
                        if (axis_x > 0.5f) last_nav_time = current_time;
                    }

                    if (IsGamepadButtonPressed(0, 2)) select = true;
                    if (IsGamepadButtonPressed(0, 1)) back = true;
                    if (IsGamepadButtonPressed(0, 3)) toggle_notif = true;
                    if (IsGamepadButtonPressed(0, 0)) trigger_update = true;
                } else {
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.5f && (current_time - last_nav_time > 0.3))) {
                        move_left = true;
                        if (axis_x < -0.5f) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.5f && (current_time - last_nav_time > 0.3))) {
                        move_right = true;
                        if (axis_x > 0.5f) last_nav_time = current_time;
                    }

                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) select = true;
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) toggle_notif = true;
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)) trigger_update = true;
                }
            }

            if (toggle_notif) {
                show_notifications = !show_notifications;
                if (show_notifications) {
                    pthread_mutex_lock(&notif_mutex);
                    unread_notifications = 0;
                    pthread_mutex_unlock(&notif_mutex);
                }
            }

            bool can_navigate = !show_notifications;

            if (can_navigate) {
                int prev_selection = current_selection;
                if (move_left) {
                    current_selection--;
                    if (current_selection < 0) current_selection = 0;
                }
                if (move_right) {
                    current_selection++;
                    if (current_selection >= MENU_ITEM_COUNT) current_selection = MENU_ITEM_COUNT - 1;
                }

                if (current_selection != prev_selection) {
                    if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
                }

                if (select) {
                    if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
                    if (current_selection == 3) {
                        pthread_mutex_lock(&update_mutex);
                        current_state = STATE_SETTINGS;
                        pthread_mutex_unlock(&update_mutex);
                    } else {
                        printf("Selected: %s\n", menu_items[current_selection]);
                    }
                }

                if (back) {
                    if (sound_back.stream.buffer != NULL) PlaySound(sound_back);
                    should_close = true;
                }

                pthread_mutex_lock(&update_mutex);
                bool has_update = update_available;
                pthread_mutex_unlock(&update_mutex);

                if (trigger_update && has_update) {
                    if (sound_notify.stream.buffer != NULL) PlaySound(sound_notify);
                    pthread_mutex_lock(&update_mutex);
                    current_state = STATE_UPDATING;
                    update_in_progress = true;
                    pthread_mutex_unlock(&update_mutex);

                    pthread_t installer_thread;
                    pthread_create(&installer_thread, NULL, UpdateInstallerThread, NULL);
                    pthread_detach(installer_thread);
                }
            }

            // Animate cards
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                float target_scale = (i == current_selection) ? 1.1f : 1.0f;
                float target_y = (i == current_selection) ? -20.0f : 0.0f;
                card_scales[i] += (target_scale - card_scales[i]) * 15.0f * dt;
                card_y_offsets[i] += (target_y - card_y_offsets[i]) * 15.0f * dt;
            }

            // Animate notification drawer
            float target_notif_x = show_notifications ? (SCREEN_WIDTH - 400) : SCREEN_WIDTH;
            notif_drawer_x += (target_notif_x - notif_drawer_x) * 15.0f * dt;

        } else if (state_copy == STATE_UPDATING) {
            spinner_angle += 180.0f * dt;

            pthread_mutex_lock(&update_mutex);
            bool success = update_success;
            bool failed = update_failed;
            pthread_mutex_unlock(&update_mutex);

            if (success) {
                static bool success_sound_played = false;
                if (!success_sound_played) {
                    if (sound_notify.stream.buffer != NULL) PlaySound(sound_notify);
                    success_sound_played = true;
                }
                should_close = true; // Clean exit
            }

            if (failed) {
                bool back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
                if (back) {
                    if (sound_back.stream.buffer != NULL) PlaySound(sound_back);

                    pthread_mutex_lock(&update_mutex);
                    current_state = STATE_DASHBOARD;
                    update_in_progress = false;
                    update_failed = false;
                    strcpy(update_status_text, "Initializing...");
                    pthread_mutex_unlock(&update_mutex);
                }
            }
        } else if (state_copy == STATE_SETTINGS) {
            bool move_left = IsKeyPressed(KEY_LEFT);
            bool move_right = IsKeyPressed(KEY_RIGHT);
            bool toggle_mute = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);
            bool back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B);

            if (IsGamepadAvailable(0)) {
                ControllerProfile effective_profile = active_profile;
                if (effective_profile == PROFILE_AUTO) {
                    const char* gp_name = GetGamepadName(0);
                    if (gp_name != NULL && (strstr(gp_name, "Sony") != NULL || strstr(gp_name, "DualSense") != NULL || strstr(gp_name, "PS5") != NULL)) {
                        effective_profile = PROFILE_PS5;
                    } else {
                        effective_profile = PROFILE_XBOX;
                    }
                }

                static double last_nav_time = 0;
                double current_time = GetTime();

                float axis_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
                if (fabs(axis_x) < 0.25f) axis_x = 0.0f; // Deadzone

                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.5f && (current_time - last_nav_time > 0.3))) {
                    move_left = true;
                    if (axis_x < -0.5f) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.5f && (current_time - last_nav_time > 0.3))) {
                    move_right = true;
                    if (axis_x > 0.5f) last_nav_time = current_time;
                }

                if (effective_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(0, 1)) back = true;
                } else {
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                }
            }

            if (toggle_mute) {
                if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
                bgm_muted = !bgm_muted;
                if (bgm.stream.buffer != NULL) {
                    SetMusicVolume(bgm, bgm_muted ? 0.0f : 0.2f);
                }
            }

            ControllerProfile prev_profile = active_profile;
            if (move_left) {
                if (active_profile > 0) active_profile--;
            }
            if (move_right) {
                if (active_profile < 3) active_profile++;
            }

            if (active_profile != prev_profile) {
                if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
            }

            if (back) {
                if (sound_back.stream.buffer != NULL) PlaySound(sound_back);
                pthread_mutex_lock(&update_mutex);
                current_state = STATE_DASHBOARD;
                pthread_mutex_unlock(&update_mutex);
            }
        }

        BeginDrawing();
        ClearBackground(COLOR_BG);

        pthread_mutex_lock(&update_mutex);
        AppUIState render_state = current_state;
        pthread_mutex_unlock(&update_mutex);

        if (render_state == STATE_DASHBOARD) {
            // Main Grid
            float card_base_width = 340;
            float card_base_height = 500;
            float spacing = 60;
            float total_width = (MENU_ITEM_COUNT * card_base_width) + ((MENU_ITEM_COUNT - 1) * spacing);
            float start_x = (SCREEN_WIDTH - total_width) / 2.0f;
            float center_y = SCREEN_HEIGHT / 2.0f + 30;

            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                float scale = card_scales[i];
                float w = card_base_width * scale;
                float h = card_base_height * scale;
                float x = start_x + i * (card_base_width + spacing) + (card_base_width / 2.0f);
                float y = center_y + card_y_offsets[i];

                Rectangle rect = { x - w / 2.0f, y - h / 2.0f, w, h };

                if (i == current_selection) {
                    Rectangle glow_rect = { rect.x - 6, rect.y - 6, rect.width + 12, rect.height + 12 };
                    DrawRectangleRounded(glow_rect, 0.1f, 16, COLOR_ACCENT);
                    DrawRectangleRounded(rect, 0.1f, 16, COLOR_CARD_FOCUS);
                } else {
                    DrawRectangleRounded(rect, 0.1f, 16, COLOR_CARD_IDLE);
                    DrawRectangleRoundedLinesEx(rect, 0.1f, 16, 1.0f, COLOR_TEXT_MUTED);
                }

                if (tex_icons[i].id > 0) {
                    float max_icon_size = w * 0.5f;
                    float tex_scale = 1.0f;
                    if (tex_icons[i].width > max_icon_size) tex_scale = max_icon_size / tex_icons[i].width;

                    Vector2 pos = {
                        x - (tex_icons[i].width * tex_scale) / 2.0f,
                        y - (tex_icons[i].height * tex_scale) / 2.0f - 40
                    };
                    Color tint = (i == current_selection) ? WHITE : (Color){200, 200, 200, 255};
                    DrawTextureEx(tex_icons[i], pos, 0.0f, tex_scale, tint);
                } else {
                    Rectangle fallback_rect = { x - 50, y - 50 - 40, 100, 100 };
                    DrawRectangleRounded(fallback_rect, 0.2f, 8, COLOR_ACCENT);
                }

                int text_width = MeasureText(menu_items[i], 32);
                Color text_color = (i == current_selection) ? COLOR_TEXT_MAIN : COLOR_TEXT_MUTED;
                DrawText(menu_items[i], x - text_width / 2, y + h / 2.0f - 60, 32, text_color);
            }

            // Top Bar
            DrawRectangle(0, 0, SCREEN_WIDTH, 60, COLOR_TOPBAR);
            DrawText("CONSOLE-BOX", 40, 20, 22, COLOR_TEXT_MAIN);

            time_t t = time(NULL);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);

            int time_width = MeasureText(time_str, 22);
            DrawText(time_str, SCREEN_WIDTH - time_width - 40, 20, 22, COLOR_TEXT_MAIN);

            // Network / Status
            DrawText("Network: UP", SCREEN_WIDTH - time_width - 180, 20, 20, COLOR_TEXT_MUTED);

            // Notifications Bell
            DrawText("Bell (X)", SCREEN_WIDTH - time_width - 320, 20, 20, COLOR_TEXT_MAIN);

            pthread_mutex_lock(&notif_mutex);
            int unread = unread_notifications;
            pthread_mutex_unlock(&notif_mutex);

            if (unread > 0) {
                DrawCircle(SCREEN_WIDTH - time_width - 230, 20, 6, COLOR_ACCENT);
            }

            // Update Chip
            pthread_mutex_lock(&update_mutex);
            bool has_update = update_available;
            pthread_mutex_unlock(&update_mutex);
            if (has_update) {
                DrawRectangleRounded((Rectangle){SCREEN_WIDTH/2 - 100, 12, 200, 36}, 0.5f, 10, COLOR_ACCENT);
                DrawText("Update Available", SCREEN_WIDTH/2 - MeasureText("Update Available", 20)/2, 20, 20, COLOR_BG);
            }

            // Bottom Bar
            const char* legend = "(A) Select   (B) Back   (X) Notifications   (Y) Check Updates";
            ControllerProfile effective_profile = active_profile;
            if (effective_profile == PROFILE_AUTO) {
                const char* gp_name = GetGamepadName(0);
                if (gp_name != NULL && (strstr(gp_name, "Sony") != NULL || strstr(gp_name, "DualSense") != NULL || strstr(gp_name, "PS5") != NULL)) {
                    effective_profile = PROFILE_PS5;
                }
            }
            if (effective_profile == PROFILE_PS5) {
                legend = "(✖) Select   (⭘) Back   (◼) Notifications   (▲) Check Updates";
            }
            int legend_width = MeasureText(legend, 20);
            DrawText(legend, (SCREEN_WIDTH - legend_width) / 2, SCREEN_HEIGHT - 40, 20, COLOR_TEXT_MUTED);

            // Notification Drawer
            if (notif_drawer_x < SCREEN_WIDTH) {
                DrawRectangle(notif_drawer_x, 60, 400, SCREEN_HEIGHT - 60, (Color){20, 25, 30, 240});
                DrawLine(notif_drawer_x, 60, notif_drawer_x, SCREEN_HEIGHT, COLOR_ACCENT);
                DrawText("Notifications", notif_drawer_x + 20, 80, 24, COLOR_ACCENT);

                pthread_mutex_lock(&notif_mutex);
                for (int i = 0; i < notification_count; i++) {
                    int idx = notification_count - 1 - i; // reverse order
                    if (i > 10) break;
                    DrawText(notifications[idx], notif_drawer_x + 20, 130 + i * 40, 20, COLOR_TEXT_MAIN);
                }
                pthread_mutex_unlock(&notif_mutex);
            }

        } else if (render_state == STATE_SETTINGS) {
            DrawRectangle(0, 0, SCREEN_WIDTH, 60, COLOR_TOPBAR);
            DrawText("Controller & Input", 40, 20, 22, COLOR_TEXT_MAIN);

            float cx = SCREEN_WIDTH / 2.0f;
            float cy = SCREEN_HEIGHT / 2.0f;

            const char* gp_name = IsGamepadAvailable(0) ? GetGamepadName(0) : "No Gamepad Detected";
            int nw = MeasureText(gp_name, 24);
            DrawText(gp_name, cx - nw / 2, cy - 200, 24, COLOR_TEXT_MAIN);

            const char* profile_names[] = { "[ Auto Detect ]", "[ Modern Xbox ]", "[ PlayStation 5 ]", "[ PS2 Legacy (USB Adapter) ]" };

            float total_pw = 0;
            float spacing = 20;
            for(int i=0; i<4; i++) total_pw += MeasureText(profile_names[i], 20);
            total_pw += spacing * 3;

            float px = cx - total_pw / 2;
            for (int i = 0; i < 4; i++) {
                int pw = MeasureText(profile_names[i], 20);
                Color color = (i == (int)active_profile) ? COLOR_ACCENT : COLOR_TEXT_MUTED;
                DrawText(profile_names[i], px, cy - 140, 20, color);
                px += pw + spacing;
            }

            // Visual Button Test Overlay
            float bx = cx + 200;
            float by = cy + 50;
            float br = 20.0f;

            ControllerProfile effective_profile = active_profile;
            if (effective_profile == PROFILE_AUTO) {
                if (gp_name != NULL && (strstr(gp_name, "Sony") != NULL || strstr(gp_name, "DualSense") != NULL || strstr(gp_name, "PS5") != NULL)) {
                    effective_profile = PROFILE_PS5;
                } else {
                    effective_profile = PROFILE_XBOX;
                }
            }

            int btn_y = (effective_profile == PROFILE_PS2_LEGACY) ? 0 : GAMEPAD_BUTTON_RIGHT_FACE_UP;
            int btn_x = (effective_profile == PROFILE_PS2_LEGACY) ? 3 : GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
            int btn_a = (effective_profile == PROFILE_PS2_LEGACY) ? 2 : GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
            int btn_b = (effective_profile == PROFILE_PS2_LEGACY) ? 1 : GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;

            Color c_y = IsGamepadButtonDown(0, btn_y) ? COLOR_ACCENT : COLOR_CARD_IDLE;
            Color c_x = IsGamepadButtonDown(0, btn_x) ? COLOR_ACCENT : COLOR_CARD_IDLE;
            Color c_a = IsGamepadButtonDown(0, btn_a) ? COLOR_ACCENT : COLOR_CARD_IDLE;
            Color c_b = IsGamepadButtonDown(0, btn_b) ? COLOR_ACCENT : COLOR_CARD_IDLE;

            DrawCircle(bx, by - 40, br, c_y);
            DrawCircleLines(bx, by - 40, br, COLOR_TEXT_MUTED);

            DrawCircle(bx - 40, by, br, c_x);
            DrawCircleLines(bx - 40, by, br, COLOR_TEXT_MUTED);

            DrawCircle(bx, by + 40, br, c_a);
            DrawCircleLines(bx, by + 40, br, COLOR_TEXT_MUTED);

            DrawCircle(bx + 40, by, br, c_b);
            DrawCircleLines(bx + 40, by, br, COLOR_TEXT_MUTED);

            // Stick Crosshair
            float sx = cx - 200;
            float sy = cy + 50;
            Rectangle stick_box = { sx - 60, sy - 60, 120, 120 };
            DrawRectangleRoundedLinesEx(stick_box, 0.2f, 16, 2.0f, COLOR_TEXT_MUTED);

            float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            float ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
            if (fabs(ax) < 0.25f) ax = 0.0f;
            if (fabs(ay) < 0.25f) ay = 0.0f;

            DrawCircle(sx + ax * 60, sy + ay * 60, 10, COLOR_ACCENT);

            // Audio Toggle
            float audio_y = cy + 180;
            const char* audio_text = bgm_muted ? "BGM: Muted" : "BGM: Playing";
            Color audio_color = bgm_muted ? COLOR_TEXT_MUTED : COLOR_ACCENT;
            DrawRectangleRounded((Rectangle){ cx - 100, audio_y - 20, 200, 40 }, 0.5f, 10, bgm_muted ? COLOR_CARD_IDLE : COLOR_CARD_FOCUS);
            int aw = MeasureText(audio_text, 20);
            DrawText(audio_text, cx - aw / 2, audio_y - 10, 20, audio_color);

            const char* legend = "(Up/Down/A) Toggle Mute   (Cancel/Back) Return";
            int lw = MeasureText(legend, 20);
            DrawText(legend, cx - lw / 2, SCREEN_HEIGHT - 40, 20, COLOR_TEXT_MUTED);

        } else if (render_state == STATE_UPDATING) {
            pthread_mutex_lock(&update_mutex);
            bool failed = update_failed;
            char status_copy[256];
            strcpy(status_copy, update_status_text);
            pthread_mutex_unlock(&update_mutex);

            if (!failed) {
                // Spinning radar
                Vector2 center = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f - 50 };
                DrawRing(center, 80.0f, 100.0f, spinner_angle, spinner_angle + 90.0f, 64, COLOR_ACCENT);
                DrawRing(center, 80.0f, 100.0f, spinner_angle + 180.0f, spinner_angle + 270.0f, 64, COLOR_ACCENT);

                int sw = MeasureText(status_copy, 30);
                DrawText(status_copy, SCREEN_WIDTH / 2 - sw / 2, SCREEN_HEIGHT / 2 + 100, 30, COLOR_TEXT_MAIN);
            } else {
                int ew = MeasureText("UPDATE FAILED", 40);
                DrawText("UPDATE FAILED", SCREEN_WIDTH / 2 - ew / 2, SCREEN_HEIGHT / 2 - 50, 40, COLOR_ERROR);
                int sw = MeasureText(status_copy, 24);
                DrawText(status_copy, SCREEN_WIDTH / 2 - sw / 2, SCREEN_HEIGHT / 2 + 10, 24, COLOR_TEXT_MUTED);

                const char* back_msg = "Press (B) or ESC to return to Dashboard";
                int bw = MeasureText(back_msg, 20);
                DrawText(back_msg, SCREEN_WIDTH / 2 - bw / 2, SCREEN_HEIGHT / 2 + 80, 20, COLOR_TEXT_MAIN);
            }
        }

        EndDrawing();
    }

    // Unload textures
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (tex_icons[i].id > 0) {
            UnloadTexture(tex_icons[i]);
        }
    }

    if (sound_nav.stream.buffer != NULL) UnloadSound(sound_nav);
    if (sound_select.stream.buffer != NULL) UnloadSound(sound_select);
    if (sound_back.stream.buffer != NULL) UnloadSound(sound_back);
    if (sound_notify.stream.buffer != NULL) UnloadSound(sound_notify);

    if (bgm.stream.buffer != NULL) UnloadMusicStream(bgm);

    CloseAudioDevice();
    CloseWindow();

    return 0;
}
