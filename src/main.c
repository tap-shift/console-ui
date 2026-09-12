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
static const Color COLOR_BG = { 11, 14, 20, 255 }; // Deep slate
static const Color COLOR_CARD_IDLE = { 25, 30, 40, 160 }; // Frosted panel
static const Color COLOR_CARD_FOCUS = { 35, 42, 55, 220 }; // Brighter panel
static const Color COLOR_ACCENT = { 0, 210, 255, 255 }; // Electric cyan
static const Color COLOR_TEXT_MAIN = { 240, 240, 240, 255 };
static const Color COLOR_TEXT_MUTED = { 150, 150, 150, 255 };
static const Color COLOR_TOPBAR = { 13, 17, 23, 220 };
static const Color COLOR_ERROR = { 255, 80, 80, 255 };

#define MAX_MENU_ITEMS 64
int image_count = 0;
char* image_names[MAX_MENU_ITEMS];
Texture2D tex_icons[MAX_MENU_ITEMS];
float card_scales[MAX_MENU_ITEMS];
float card_y_offsets[MAX_MENU_ITEMS];

typedef enum {
    STATE_DASHBOARD,
    STATE_UPDATING,
    STATE_SETTINGS,
    STATE_CONTROLLER_TEST
} AppUIState;

AppUIState current_state = STATE_DASHBOARD;

typedef enum {
    PROFILE_AUTO = 0,
    PROFILE_XBOX,
    PROFILE_PS5,
    PROFILE_PS2_LEGACY
} ControllerProfile;

ControllerProfile active_profile = PROFILE_AUTO;

int settings_tab = 0;
int settings_row = 0;
bool settings_focus_right_pane = false;
int active_audio_device = 0;

#define MAX_AUDIO_SINKS 16
char* actual_audio_sinks[MAX_AUDIO_SINKS];
int actual_audio_sink_count = 0;
const char* audio_sinks[2] = {
    "Built-in Audio Analog Stereo",
    "DualSense Wireless Controller Audio"
};

void PopulateAudioDevices() {
    FILE* fp = popen("pactl list short sinks 2>/dev/null | awk '{print $2}'", "r");
    if (fp != NULL) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != NULL && actual_audio_sink_count < MAX_AUDIO_SINKS) {
            buffer[strcspn(buffer, "\n")] = 0;
            actual_audio_sinks[actual_audio_sink_count] = strdup(buffer);

            // Prioritize analog output
            if (strstr(buffer, "analog") != NULL || strstr(buffer, "alc") != NULL || strstr(buffer, "realtek") != NULL) {
                active_audio_device = actual_audio_sink_count; // Set as default if matched
            }

            actual_audio_sink_count++;
        }
        pclose(fp);
    }
}

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
    PopulateAudioDevices();
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Console UI");
    SetTargetFPS(60);
    ToggleFullscreen();
    InitAudioDevice();
    SetMasterVolume(2.0f); // High-end software boost gain

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

    for (int i = 0; i < MAX_MENU_ITEMS; i++) {
        tex_icons[i] = (Texture2D){0};
        card_scales[i] = 1.0f;
        card_y_offsets[i] = 0.0f;
    }

    FilePathList files = LoadDirectoryFiles("assets/images");
    for (int i = 0; i < (int)files.count && image_count < MAX_MENU_ITEMS; i++) {
        if (IsFileExtension(files.paths[i], ".png")) {
            tex_icons[image_count] = LoadTexture(files.paths[i]);
            SetTextureFilter(tex_icons[image_count], TEXTURE_FILTER_BILINEAR);

            const char* fname = GetFileNameWithoutExt(files.paths[i]);
            image_names[image_count] = strdup(fname);
            image_count++;
        }
    }
    UnloadDirectoryFiles(files);

    // Procedural Fallback if empty
    if (image_count == 0) {
        image_names[0] = strdup("Library");
        image_names[1] = strdup("Media");
        image_names[2] = strdup("Terminal");
        image_names[3] = strdup("Settings");
        image_count = 4;
    }

    SetExitKey(KEY_NULL);

    int current_selection = 0;
    int dock_selection = -1; // -1 means focus is on the shelf
    bool should_close = false;
    float spinner_angle = 0.0f;
    float camera_offset_x = 0.0f;
    float bg_fade = 0.0f;
    int last_selection = 0;
    Texture2D last_bg_tex = {0};

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
            bool move_up = IsKeyPressed(KEY_UP);
            bool move_down = IsKeyPressed(KEY_DOWN);
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
                float axis_y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
                if (fabs(axis_x) < 0.25f) axis_x = 0.0f; // Deadzone
                if (fabs(axis_y) < 0.25f) axis_y = 0.0f;

                if (effective_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.5f && (current_time - last_nav_time > 0.3))) {
                        move_left = true;
                        if (axis_x < -0.5f) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.5f && (current_time - last_nav_time > 0.3))) {
                        move_right = true;
                        if (axis_x > 0.5f) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.5f && (current_time - last_nav_time > 0.3))) {
                        move_up = true;
                        if (axis_y < -0.5f) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.5f && (current_time - last_nav_time > 0.3))) {
                        move_down = true;
                        if (axis_y > 0.5f) last_nav_time = current_time;
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
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.5f && (current_time - last_nav_time > 0.3))) {
                        move_up = true;
                        if (axis_y < -0.5f) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.5f && (current_time - last_nav_time > 0.3))) {
                        move_down = true;
                        if (axis_y > 0.5f) last_nav_time = current_time;
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
                int prev_sel = current_selection;
                int prev_dock = dock_selection;

                if (move_down && dock_selection == -1) {
                    dock_selection = 0; // Focus dock
                } else if (move_up && dock_selection != -1) {
                    dock_selection = -1; // Focus shelf
                }

                if (dock_selection == -1) {
                    if (move_left) {
                        current_selection--;
                        if (current_selection < 0) current_selection = 0;
                    }
                    if (move_right) {
                        current_selection++;
                        if (current_selection >= image_count) current_selection = image_count - 1;
                    }
                } else {
                    if (move_left) {
                        dock_selection--;
                        if (dock_selection < 0) dock_selection = 0;
                    }
                    if (move_right) {
                        dock_selection++;
                        if (dock_selection > 3) dock_selection = 3;
                    }
                }

                if (current_selection != prev_sel || dock_selection != prev_dock) {
                    if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
                }

                if (select) {
                    if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
                    if (dock_selection == -1) {
                        if (current_selection < image_count) {
                            printf("Selected: %s\n", image_names[current_selection]);
                        }
                    } else {
                        if (dock_selection == 1) { // Settings
                            pthread_mutex_lock(&update_mutex);
                            current_state = STATE_SETTINGS;
                            pthread_mutex_unlock(&update_mutex);
                        } else if (dock_selection == 3) { // Power
                            should_close = true;
                        }
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
            for (int i = 0; i < image_count; i++) {
                float target_scale = (i == current_selection) ? 1.05f : 1.0f;
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
            bool move_up = IsKeyPressed(KEY_UP);
            bool move_down = IsKeyPressed(KEY_DOWN);
            bool confirm = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER);
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
                float axis_y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
                if (fabs(axis_x) < 0.25f) axis_x = 0.0f; // Deadzone
                if (fabs(axis_y) < 0.25f) axis_y = 0.0f;

                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.5f && (current_time - last_nav_time > 0.3))) {
                    move_left = true;
                    if (axis_x < -0.5f) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.5f && (current_time - last_nav_time > 0.3))) {
                    move_right = true;
                    if (axis_x > 0.5f) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.5f && (current_time - last_nav_time > 0.3))) {
                    move_up = true;
                    if (axis_y < -0.5f) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.5f && (current_time - last_nav_time > 0.3))) {
                    move_down = true;
                    if (axis_y > 0.5f) last_nav_time = current_time;
                }

                if (effective_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(0, 1)) back = true;
                    if (IsGamepadButtonPressed(0, 2)) confirm = true;
                } else {
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) confirm = true;
                }
            }

            int prev_tab = settings_tab;
            int prev_row = settings_row;
            bool prev_focus = settings_focus_right_pane;

            if (move_left) {
                settings_focus_right_pane = false;
            }
            if (move_right) {
                settings_focus_right_pane = true;
            }

            int max_rows = 1;
            if (settings_tab == 0) max_rows = 2; // Audio has 2 rows
            if (settings_tab == 1) max_rows = 2; // Input has 2 rows (profile, test)

            if (!settings_focus_right_pane) {
                if (move_up) {
                    settings_tab--;
                    if (settings_tab < 0) settings_tab = 0;
                    settings_row = 0;
                }
                if (move_down) {
                    settings_tab++;
                    if (settings_tab > 3) settings_tab = 3;
                    settings_row = 0;
                }
            } else {
                if (move_up) {
                    settings_row--;
                    if (settings_row < 0) settings_row = 0;
                }
                if (move_down) {
                    settings_row++;
                    if (settings_row >= max_rows) settings_row = max_rows - 1;
                }
            }

            if (settings_tab != prev_tab || settings_row != prev_row || settings_focus_right_pane != prev_focus) {
                if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
            }

            if (confirm && !settings_focus_right_pane) {
                settings_focus_right_pane = true;
                if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
            } else if (confirm && settings_focus_right_pane) {
                if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
                if (settings_tab == 0 && settings_row == 0) {
                    // Toggle Mute
                    bgm_muted = !bgm_muted;
                    if (bgm.stream.buffer != NULL) {
                        SetMusicVolume(bgm, bgm_muted ? 0.0f : 0.2f);
                    }
                } else if (settings_tab == 0 && settings_row == 1) {
                    // Cycle Audio Sink
                    if (actual_audio_sink_count > 0) {
                        active_audio_device = (active_audio_device + 1) % actual_audio_sink_count;
                        char cmd[512];
                        snprintf(cmd, sizeof(cmd), "pactl set-default-sink %s > /dev/null 2>&1 &", actual_audio_sinks[active_audio_device]);
                        system(cmd);
                    }
                } else if (settings_tab == 1 && settings_row == 0) {
                    // Cycle Input Profile
                    if (active_profile < 3) active_profile++;
                    else active_profile = 0;
                } else if (settings_tab == 1 && settings_row == 1) {
                    // Enter Controller Test
                    pthread_mutex_lock(&update_mutex);
                    current_state = STATE_CONTROLLER_TEST;
                    pthread_mutex_unlock(&update_mutex);
                }
            }

            if (back) {
                if (sound_back.stream.buffer != NULL) PlaySound(sound_back);
                pthread_mutex_lock(&update_mutex);
                current_state = STATE_DASHBOARD;
                pthread_mutex_unlock(&update_mutex);
            }
        } else if (state_copy == STATE_CONTROLLER_TEST) {
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

                if (effective_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(0, 1)) back = true;
                } else {
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE)) back = true; // PS button / Guide
                }
            }

            if (back) {
                if (sound_back.stream.buffer != NULL) PlaySound(sound_back);
                pthread_mutex_lock(&update_mutex);
                current_state = STATE_SETTINGS;
                pthread_mutex_unlock(&update_mutex);
            }
        }

        BeginDrawing();
        ClearBackground(COLOR_BG);

        pthread_mutex_lock(&update_mutex);
        AppUIState render_state = current_state;
        pthread_mutex_unlock(&update_mutex);

        if (render_state == STATE_DASHBOARD) {
            // Dynamic Backdrop Cross-fade
            if (current_selection != last_selection) {
                last_bg_tex = tex_icons[last_selection];
                last_selection = current_selection;
                bg_fade = 0.0f;
            }
            if (bg_fade < 1.0f) {
                bg_fade += 2.0f * dt;
                if (bg_fade > 1.0f) bg_fade = 1.0f;
            }

            if (image_count > 0) {
                if (last_bg_tex.id > 0) {
                    float scale_x = (float)SCREEN_WIDTH / last_bg_tex.width;
                    float scale_y = (float)SCREEN_HEIGHT / last_bg_tex.height;
                    float scale = (scale_x > scale_y) ? scale_x : scale_y;
                    Vector2 pos = { (SCREEN_WIDTH - last_bg_tex.width * scale) / 2.0f, (SCREEN_HEIGHT - last_bg_tex.height * scale) / 2.0f };
                    DrawTextureEx(last_bg_tex, pos, 0.0f, scale, Fade((Color){60, 60, 70, 255}, 1.0f - bg_fade));
                }
                if (current_selection < image_count && tex_icons[current_selection].id > 0) {
                    Texture2D bg_tex = tex_icons[current_selection];
                    float scale_x = (float)SCREEN_WIDTH / bg_tex.width;
                    float scale_y = (float)SCREEN_HEIGHT / bg_tex.height;
                    float scale = (scale_x > scale_y) ? scale_x : scale_y;
                    Vector2 pos = { (SCREEN_WIDTH - bg_tex.width * scale) / 2.0f, (SCREEN_HEIGHT - bg_tex.height * scale) / 2.0f };
                    DrawTextureEx(bg_tex, pos, 0.0f, scale, Fade((Color){60, 60, 70, 255}, bg_fade));
                }
            }

            // Main Grid
            float card_base_width = 340;
            float card_base_height = 500;
            float spacing = 60;
            float center_y = SCREEN_HEIGHT / 2.0f + 30;

            // Target camera offset points to center the selected card
            float target_camera_x = -((current_selection * (card_base_width + spacing)) + (card_base_width / 2.0f));
            camera_offset_x += (target_camera_x - camera_offset_x) * 15.0f * dt;

            // Base x is center of screen + camera offset
            float base_x = SCREEN_WIDTH / 2.0f + camera_offset_x;

            for (int i = 0; i < image_count; i++) {
                float scale = card_scales[i];
                float w = card_base_width * scale;
                float h = card_base_height * scale;
                float x = base_x + i * (card_base_width + spacing) + (card_base_width / 2.0f);
                float y = center_y + card_y_offsets[i];

                Rectangle rect = { x - w / 2.0f, y - h / 2.0f, w, h };

                if (i == current_selection) {
                    Rectangle glow_rect = { rect.x - 8, rect.y - 8, rect.width + 16, rect.height + 16 };
                    DrawRectangleRounded(glow_rect, 0.15f, 32, Fade(COLOR_ACCENT, 0.4f));
                    DrawRectangleRounded(rect, 0.15f, 32, COLOR_CARD_FOCUS);
                    DrawRectangleRoundedLinesEx(rect, 0.15f, 32, 2.0f, COLOR_ACCENT); // micro-border
                } else {
                    DrawRectangleRounded(rect, 0.15f, 32, COLOR_CARD_IDLE);
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
                    DrawRectangleRounded(fallback_rect, 0.15f, 16, COLOR_ACCENT);
                }

                int text_width = MeasureText(image_names[i], 32);
                Color text_color = (i == current_selection) ? COLOR_TEXT_MAIN : COLOR_TEXT_MUTED;
                DrawText(image_names[i], x - text_width / 2, y + h / 2.0f - 60, 32, text_color);
            }

            // Top Status Bar (Spacious & Minimal)
            char host_buffer[256] = "CONSOLE-BOX";
            gethostname(host_buffer, sizeof(host_buffer));
            DrawText(host_buffer, 40, 30, 24, COLOR_TEXT_MAIN);
            DrawText("Gamepad (Active)", 40 + MeasureText(host_buffer, 24) + 20, 32, 18, COLOR_ACCENT);

            time_t t = time(NULL);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);

            int right_anchor = SCREEN_WIDTH - 40;

            // Bell
            pthread_mutex_lock(&notif_mutex);
            int unread = unread_notifications;
            pthread_mutex_unlock(&notif_mutex);
            int bell_w = MeasureText("Bell (X)", 20);
            right_anchor -= bell_w;
            DrawText("Bell (X)", right_anchor, 32, 20, COLOR_TEXT_MAIN);
            if (unread > 0) DrawCircle(right_anchor + bell_w + 10, 32 + 10, 6, COLOR_ACCENT);
            right_anchor -= 30;

            // Network
            int net_w = MeasureText("Wi-Fi", 20);
            right_anchor -= net_w;
            DrawText("Wi-Fi", right_anchor, 32, 20, COLOR_TEXT_MUTED);
            right_anchor -= 30;

            // Audio
            int aud_w = MeasureText("[Audio Out]", 20);
            right_anchor -= aud_w;
            DrawText("[Audio Out]", right_anchor, 32, 20, COLOR_ACCENT);
            right_anchor -= 30;

            // Clock
            int time_w = MeasureText(time_str, 24);
            right_anchor -= time_w;
            DrawText(time_str, right_anchor, 30, 24, COLOR_TEXT_MAIN);

            // Update Chip
            pthread_mutex_lock(&update_mutex);
            bool has_update = update_available;
            pthread_mutex_unlock(&update_mutex);
            if (has_update) {
                DrawRectangleRounded((Rectangle){SCREEN_WIDTH/2 - 100, 20, 200, 36}, 0.5f, 16, COLOR_ACCENT);
                DrawText("Update Available", SCREEN_WIDTH/2 - MeasureText("Update Available", 20)/2, 28, 20, COLOR_BG);
            }

            // Quick Action Dock
            const char* dock_items[] = { "Library", "Settings", "Media", "Power" };
            int dock_item_count = 4;
            int total_dock_width = 0;
            for (int i=0; i<dock_item_count; i++) total_dock_width += MeasureText(dock_items[i], 20) + 60; // 60 for padding + spacing
            int dock_x = (SCREEN_WIDTH - total_dock_width) / 2;
            int dock_y = SCREEN_HEIGHT - 120;
            for (int i=0; i<dock_item_count; i++) {
                int iw = MeasureText(dock_items[i], 20);
                Rectangle dock_rect = {dock_x, dock_y, iw + 40, 50};

                if (i == dock_selection) {
                    DrawRectangleRounded(dock_rect, 0.5f, 16, COLOR_CARD_FOCUS);
                    DrawRectangleRoundedLinesEx(dock_rect, 0.5f, 16, 2.0f, COLOR_ACCENT);
                    DrawText(dock_items[i], dock_x + 20, dock_y + 15, 20, COLOR_TEXT_MAIN);
                } else {
                    DrawRectangleRounded(dock_rect, 0.5f, 16, COLOR_CARD_IDLE);
                    DrawText(dock_items[i], dock_x + 20, dock_y + 15, 20, COLOR_TEXT_MUTED);
                }
                dock_x += iw + 60;
            }

            // Status Badge and Action Prompt for Main Content
            if (image_count > 0 && current_selection < image_count) {
                const char* status_badge = "Ready to Play";
                int sw = MeasureText(status_badge, 22);
                DrawText(status_badge, SCREEN_WIDTH / 2 - sw / 2, SCREEN_HEIGHT / 2 + 300, 22, COLOR_ACCENT);
            }

            // Footer
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
            DrawText("System Settings", 40, 20, 22, COLOR_TEXT_MAIN);

            // Left Column (Categories)
            float left_w = 400.0f;
            DrawRectangle(0, 60, left_w, SCREEN_HEIGHT - 60, COLOR_CARD_IDLE);
            if (!settings_focus_right_pane) {
                DrawRectangleLinesEx((Rectangle){0, 60, left_w, SCREEN_HEIGHT - 60}, 2.0f, COLOR_ACCENT);
            } else {
                DrawLine(left_w, 60, left_w, SCREEN_HEIGHT, Fade(COLOR_TEXT_MUTED, 0.3f));
            }

            const char* tabs[] = { "Audio & Sound", "Input & Gamepad", "Display & System", "Updates" };
            for (int i = 0; i < 4; i++) {
                float y = 100 + i * 80;
                if (settings_tab == i) {
                    DrawRectangle(0, y - 10, left_w, 60, COLOR_CARD_FOCUS);
                    DrawRectangle(0, y - 10, 6, 60, (!settings_focus_right_pane) ? COLOR_ACCENT : Fade(COLOR_ACCENT, 0.3f));
                    DrawText(tabs[i], 40, y + 10, 24, COLOR_TEXT_MAIN);
                } else {
                    DrawText(tabs[i], 40, y + 10, 24, COLOR_TEXT_MUTED);
                }
            }

            // Right Column (Items)
            float right_x = left_w + 40;
            float right_y = 100;
            float max_w = SCREEN_WIDTH - right_x - 80;

            if (settings_tab == 0) { // Audio
                DrawText("Audio Output Configuration", right_x, right_y, 30, COLOR_TEXT_MAIN);
                right_y += 60;

                // Mute Row
                Color r_color = (settings_row == 0 && settings_focus_right_pane) ? COLOR_CARD_FOCUS : COLOR_CARD_IDLE;
                if (settings_row == 0) DrawRectangleRounded((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, r_color);
                if (settings_row == 0 && settings_focus_right_pane) DrawRectangleRoundedLinesEx((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, 2.0f, COLOR_ACCENT);

                DrawText("Mute Master Audio", right_x, right_y, 24, COLOR_TEXT_MAIN);
                DrawRectangleRounded((Rectangle){right_x + max_w - 120, right_y - 5, 120, 40}, 1.0f, 32, bgm_muted ? COLOR_CARD_IDLE : COLOR_ACCENT);
                DrawText(bgm_muted ? "OFF" : "ON", right_x + max_w - 80, right_y + 5, 20, bgm_muted ? COLOR_TEXT_MUTED : COLOR_BG);
                right_y += 80;

                // Sink Row
                extern int actual_audio_sink_count; // Defined later
                extern char* actual_audio_sinks[]; // Defined later
                r_color = (settings_row == 1 && settings_focus_right_pane) ? COLOR_CARD_FOCUS : COLOR_CARD_IDLE;
                if (settings_row == 1) DrawRectangleRounded((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, r_color);
                if (settings_row == 1 && settings_focus_right_pane) DrawRectangleRoundedLinesEx((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, 2.0f, COLOR_ACCENT);

                DrawText("Output Device", right_x, right_y, 24, COLOR_TEXT_MAIN);
                const char* disp_name = (actual_audio_sink_count > 0 && active_audio_device < actual_audio_sink_count) ? actual_audio_sinks[active_audio_device] : audio_sinks[0];
                int dev_w = MeasureText(disp_name, 20);
                DrawText(disp_name, right_x + max_w - dev_w - 20, right_y + 10, 20, (settings_row == 1 && settings_focus_right_pane) ? COLOR_ACCENT : COLOR_TEXT_MUTED);

            } else if (settings_tab == 1) { // Input
                DrawText("Controller Configuration", right_x, right_y, 30, COLOR_TEXT_MAIN);
                right_y += 60;

                const char* profile_names[] = { "Auto Detect", "Modern Xbox", "PlayStation 5", "PS2 Legacy" };

                // Profile Row
                Color r_color = (settings_row == 0 && settings_focus_right_pane) ? COLOR_CARD_FOCUS : COLOR_CARD_IDLE;
                if (settings_row == 0) DrawRectangleRounded((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, r_color);
                if (settings_row == 0 && settings_focus_right_pane) DrawRectangleRoundedLinesEx((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, 2.0f, COLOR_ACCENT);
                DrawText("Active Profile", right_x, right_y, 24, COLOR_TEXT_MAIN);
                int pr_w = MeasureText(profile_names[(int)active_profile], 20);
                DrawText(profile_names[(int)active_profile], right_x + max_w - pr_w - 20, right_y + 10, 20, (settings_row == 0 && settings_focus_right_pane) ? COLOR_ACCENT : COLOR_TEXT_MUTED);
                right_y += 80;

                // Test Row
                r_color = (settings_row == 1 && settings_focus_right_pane) ? COLOR_CARD_FOCUS : COLOR_CARD_IDLE;
                if (settings_row == 1) DrawRectangleRounded((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, r_color);
                if (settings_row == 1 && settings_focus_right_pane) DrawRectangleRoundedLinesEx((Rectangle){right_x-10, right_y-10, max_w+20, 60}, 0.15f, 32, 2.0f, COLOR_ACCENT);
                DrawText("Test Controller Mapping", right_x, right_y, 24, COLOR_TEXT_MAIN);
                DrawText("START >", right_x + max_w - 100, right_y + 10, 20, (settings_row == 1 && settings_focus_right_pane) ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            }

            const char* legend = "(Up/Down) Select Item   (Left/Right) Change Tab / Option   (A) Confirm   (B) Back";
            int lw = MeasureText(legend, 20);
            DrawText(legend, (SCREEN_WIDTH - lw) / 2, SCREEN_HEIGHT - 40, 20, COLOR_TEXT_MUTED);

        } else if (render_state == STATE_CONTROLLER_TEST) {
            DrawRectangle(0, 0, SCREEN_WIDTH, 60, COLOR_TOPBAR);
            DrawText("Controller Test Mode", 40, 20, 22, COLOR_TEXT_MAIN);

            float cx = SCREEN_WIDTH / 2.0f;
            float cy = SCREEN_HEIGHT / 2.0f;

            DrawRectangleRounded((Rectangle){cx - 400, cy - 300, 800, 600}, 0.15f, 32, COLOR_CARD_IDLE);
            DrawRectangleRoundedLinesEx((Rectangle){cx - 400, cy - 300, 800, 600}, 0.15f, 32, 2.0f, COLOR_ACCENT);

            const char* gp_name = IsGamepadAvailable(0) ? GetGamepadName(0) : "No Gamepad Detected";
            int nw = MeasureText(gp_name, 24);
            DrawText(gp_name, cx - nw / 2, cy - 250, 24, COLOR_TEXT_MAIN);

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
            DrawRectangleRoundedLinesEx(stick_box, 0.15f, 32, 2.0f, COLOR_TEXT_MUTED);

            float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            float ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
            if (fabs(ax) < 0.25f) ax = 0.0f;
            if (fabs(ay) < 0.25f) ay = 0.0f;

            DrawCircle(sx + ax * 60, sy + ay * 60, 10, COLOR_ACCENT);

            // Triggers
            float l2 = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER);
            float r2 = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (l2 < -1.0f) l2 = -1.0f; // normalization may vary
            if (r2 < -1.0f) r2 = -1.0f;
            float l2_fill = (l2 + 1.0f) / 2.0f;
            float r2_fill = (r2 + 1.0f) / 2.0f;

            DrawRectangleLines(cx - 300, cy - 150, 40, 100, COLOR_TEXT_MUTED);
            DrawRectangle(cx - 300, cy - 150 + (1.0f - l2_fill) * 100, 40, l2_fill * 100, COLOR_ACCENT);
            DrawText("L2/LT", cx - 300, cy - 180, 20, COLOR_TEXT_MUTED);

            DrawRectangleLines(cx + 260, cy - 150, 40, 100, COLOR_TEXT_MUTED);
            DrawRectangle(cx + 260, cy - 150 + (1.0f - r2_fill) * 100, 40, r2_fill * 100, COLOR_ACCENT);
            DrawText("R2/RT", cx + 260, cy - 180, 20, COLOR_TEXT_MUTED);

            const char* legend = "(B/Circle) or (ESC/Guide) Return";
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
    for (int i = 0; i < image_count; i++) {
        if (tex_icons[i].id > 0) {
            UnloadTexture(tex_icons[i]);
        }
        if (image_names[i]) free(image_names[i]);
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
