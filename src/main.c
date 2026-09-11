#define _POSIX_C_SOURCE 200809L
#include <raylib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

// Theme colors
static const Color COLOR_BG = { 18, 18, 20, 255 };
static const Color COLOR_CARD_IDLE = { 32, 32, 36, 255 };
static const Color COLOR_CARD_FOCUS = { 45, 45, 50, 255 };
static const Color COLOR_ACCENT = { 0, 180, 255, 255 };
static const Color COLOR_TEXT_MAIN = { 240, 240, 240, 255 };
static const Color COLOR_TEXT_MUTED = { 150, 150, 150, 255 };
static const Color COLOR_TOPBAR = { 12, 12, 14, 255 };

// Menu definitions
#define MENU_ITEM_COUNT 4
static const char* menu_items[MENU_ITEM_COUNT] = {
    "Games",
    "Media",
    "Terminal / Shell",
    "Settings"
};

int main(void) {
    // Initialization
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Console UI");
    SetTargetFPS(60);

    // Set fullscreen (if running on gamescope, standard borderless works well)
    ToggleFullscreen();

    // Audio setup
    InitAudioDevice();

    // Load Sounds with clean fallback
    Sound sound_nav = {0};
    if (FileExists("assets/sounds/nav.wav")) sound_nav = LoadSound("assets/sounds/nav.wav");
    Sound sound_select = {0};
    if (FileExists("assets/sounds/select.wav")) sound_select = LoadSound("assets/sounds/select.wav");
    Sound sound_back = {0};
    if (FileExists("assets/sounds/back.wav")) sound_back = LoadSound("assets/sounds/back.wav");

    // Load Textures with clean fallback
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

    // Intercept ESC to play sound before exiting
    SetExitKey(KEY_NULL);

    // Application state
    int current_selection = 0;
    bool should_close = false;

    // Animation state
    float card_scales[MENU_ITEM_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f };

    bool gamepad_pressed_left = false;
    bool gamepad_pressed_right = false;

    // Main game loop
    while (!WindowShouldClose() && !should_close) {

        // Input Handling
        bool move_left = IsKeyPressed(KEY_LEFT);
        bool move_right = IsKeyPressed(KEY_RIGHT);
        bool select = IsKeyPressed(KEY_ENTER);
        bool back = IsKeyPressed(KEY_ESCAPE);

        if (IsGamepadAvailable(0)) {
            // D-Pad
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) move_left = true;
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) move_right = true;

            // Left Stick
            float axis_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            if (axis_x < -0.5f && !gamepad_pressed_left) {
                move_left = true;
                gamepad_pressed_left = true;
            } else if (axis_x > -0.5f) {
                gamepad_pressed_left = false;
            }

            if (axis_x > 0.5f && !gamepad_pressed_right) {
                move_right = true;
                gamepad_pressed_right = true;
            } else if (axis_x < 0.5f) {
                gamepad_pressed_right = false;
            }

            // A / Start
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
                select = true;
            }

            // B / Back
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
                back = true;
            }
        }

        if (move_left) {
            current_selection--;
            if (current_selection < 0) current_selection = 0;
            else if ((sound_nav.stream.buffer != NULL)) PlaySound(sound_nav);
        }
        if (move_right) {
            current_selection++;
            if (current_selection >= MENU_ITEM_COUNT) current_selection = MENU_ITEM_COUNT - 1;
            else if ((sound_nav.stream.buffer != NULL)) PlaySound(sound_nav);
        }

        if (select) {
            if ((sound_select.stream.buffer != NULL)) PlaySound(sound_select);
            printf("Selected: %s\n", menu_items[current_selection]);
        }

        if (back) {
            if ((sound_back.stream.buffer != NULL)) PlaySound(sound_back);
            should_close = true;
        }

        // Update Logic
        float dt = GetFrameTime();
        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            float target_scale = (i == current_selection) ? 1.05f : 1.0f;
            card_scales[i] += (target_scale - card_scales[i]) * 10.0f * dt;
        }

        // Draw Logic
        BeginDrawing();
        ClearBackground(COLOR_BG);

        // --- Top Bar ---
        DrawRectangle(0, 0, SCREEN_WIDTH, 60, COLOR_TOPBAR);
        DrawText("CONSOLE-BOX", 40, 20, 20, COLOR_TEXT_MAIN);

        // Digital clock
        time_t t = time(NULL);
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);

        const char* user_str = "User: Player 1";
        int user_width = MeasureText(user_str, 20);
        int time_width = MeasureText(time_str, 20);

        DrawText(user_str, SCREEN_WIDTH - user_width - time_width - 80, 20, 20, COLOR_TEXT_MUTED);
        DrawText(time_str, SCREEN_WIDTH - time_width - 40, 20, 20, COLOR_TEXT_MAIN);

        // --- Main Grid ---
        float card_base_width = 320;
        float card_base_height = 480;
        float spacing = 60;
        float total_width = (MENU_ITEM_COUNT * card_base_width) + ((MENU_ITEM_COUNT - 1) * spacing);
        float start_x = (SCREEN_WIDTH - total_width) / 2.0f;
        float center_y = SCREEN_HEIGHT / 2.0f;

        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            float scale = card_scales[i];
            float w = card_base_width * scale;
            float h = card_base_height * scale;
            float x = start_x + i * (card_base_width + spacing) + (card_base_width / 2.0f);
            float y = center_y;

            Rectangle rect = { x - w / 2.0f, y - h / 2.0f, w, h };

            if (i == current_selection) {
                // Glow effect (simple outer border)
                Rectangle glow_rect = { rect.x - 4, rect.y - 4, rect.width + 8, rect.height + 8 };
                DrawRectangleRounded(glow_rect, 0.1f, 16, COLOR_ACCENT);
                DrawRectangleRounded(rect, 0.1f, 16, COLOR_CARD_FOCUS);
            } else {
                DrawRectangleRounded(rect, 0.1f, 16, COLOR_CARD_IDLE);
            }

            // Draw icon or fallback
            if ((tex_icons[i].id > 0)) {
                // Scale texture down if necessary to fit nicely, center it
                float max_icon_size = w * 0.6f;
                float tex_scale = 1.0f;
                if (tex_icons[i].width > max_icon_size) tex_scale = max_icon_size / tex_icons[i].width;

                Vector2 pos = {
                    x - (tex_icons[i].width * tex_scale) / 2.0f,
                    y - (tex_icons[i].height * tex_scale) / 2.0f - 20
                };
                DrawTextureEx(tex_icons[i], pos, 0.0f, tex_scale, WHITE);
            } else {
                // Fallback graphic (accent colored rectangle)
                Rectangle fallback_rect = { x - 40, y - 40 - 20, 80, 80 };
                DrawRectangleRounded(fallback_rect, 0.2f, 8, COLOR_ACCENT);
            }

            // Draw card text
            int text_width = MeasureText(menu_items[i], 30);
            Color text_color = (i == current_selection) ? COLOR_TEXT_MAIN : COLOR_TEXT_MUTED;
            DrawText(menu_items[i], x - text_width / 2, y + h / 2.0f + 30, 30, text_color);
        }

        // --- Bottom Bar ---
        const char* legend = "[A / ENTER] Select    [D-PAD / ARROWS] Navigate    [ESC] Quit";
        int legend_width = MeasureText(legend, 20);
        DrawText(legend, (SCREEN_WIDTH - legend_width) / 2, SCREEN_HEIGHT - 60, 20, COLOR_TEXT_MUTED);

        EndDrawing();
    }

    // De-Initialization

    // Unload textures
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if ((tex_icons[i].id > 0)) {
            UnloadTexture(tex_icons[i]);
        }
    }

    // Unload sounds
    if ((sound_nav.stream.buffer != NULL)) UnloadSound(sound_nav);
    if ((sound_select.stream.buffer != NULL)) UnloadSound(sound_select);
    if ((sound_back.stream.buffer != NULL)) UnloadSound(sound_back);

    CloseAudioDevice();
    CloseWindow();

    return 0;
}
