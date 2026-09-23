        spinner_angle += 180.0f * dt;
        if (state_copy == STATE_DASHBOARD) {
```
And remove it from `STATE_UPDATING`.
And then in the render loop for `STATE_DASHBOARD`:
```c
            // Base x is center of screen + camera offset
            float base_x = SCREEN_WIDTH / 2.0f + camera_offset_x;

            if (games_fetch_pending) {
                Vector2 center = { SCREEN_WIDTH / 2.0f, center_y };
                DrawRing(center, 40.0f, 50.0f, spinner_angle, spinner_angle + 90.0f, 64, COLOR_ACCENT);
                DrawRing(center, 40.0f, 50.0f, spinner_angle + 180.0f, spinner_angle + 270.0f, 64, COLOR_ACCENT);
                DrawText("Loading library...", SCREEN_WIDTH / 2 - MeasureText("Loading library...", 20) / 2, center_y + 80, 20, COLOR_TEXT_MUTED);
            } else if (image_count == 0) {
                DrawText("No games found in your library.", SCREEN_WIDTH / 2 - MeasureText("No games found in your library.", 30) / 2, center_y, 30, COLOR_TEXT_MUTED);
                DrawText("Check your backend server connection.", SCREEN_WIDTH / 2 - MeasureText("Check your backend server connection.", 20) / 2, center_y + 50, 20, Fade(COLOR_TEXT_MUTED, 0.6f));
            }

            for (int i = 0; i < image_count; i++) {
```
This is definitely under 50 lines and vastly improves the user experience by providing an empty state AND a loading state for an async operation!

Wait, is `games_fetch_pending` accessible from the render thread without a mutex?
Let's see where it's declared and used.
`bool games_fetch_pending = true;`
It is modified in the `BackendWorkerThread` inside a mutex:
```c
                pthread_mutex_lock(&backend_mutex);
                games_fetch_pending = false;
                // ...
                pthread_mutex_unlock(&backend_mutex);
```
So I should read it using the mutex, or just read it. Since it's a bool, tearing isn't really an issue on x86, but using the mutex is safer. Wait, actually, in the logic loop:
Let me see if I can read `games_fetch_pending` in the main thread.
Yes, there's no problem reading it in the render loop because `bool` reads are generally atomic on most architectures, and it's already read in other parts of the main thread (`if (game_count == 0 && !games_fetch_pending)`). Oh wait, I saw `game_count == 0 && !games_fetch_pending` earlier, but maybe it was hallucinated?
Let's check if there is an empty state already.
Wait! I need to ensure `spinner_angle` runs. I will just add `spinner_angle += 180.0f * dt;` inside the main update block `if (state_copy == STATE_DASHBOARD)` or outside all states. Right now it's only in `STATE_UPDATING`. Wait, `spinner_angle` is also used in `STATE_UPDATING` which increments it. If I move it out, it will be shared.

Let's do this:

1. Look for `spinner_angle += 180.0f * dt;`
2. Move it to the common update section before the `if (state_copy == STATE_DASHBOARD)` switch.

Wait, if the user requested ONE micro UX improvement:
1. "Add loading spinner to async submit button" -> Add loading spinner and empty state to the main carousel.

Wait, let me make sure the empty state is not drawn if `image_count > 0`.
If `image_count == 0` but `games_fetch_pending` is true, draw spinner.
If `image_count == 0` but `games_fetch_pending` is false, draw empty state.

Let me review the `render_state == STATE_DASHBOARD` block:
```c
            // Base x is center of screen + camera offset
            float base_x = SCREEN_WIDTH / 2.0f + camera_offset_x;

            bool is_loading_games = false;
            pthread_mutex_lock(&backend_mutex);
            is_loading_games = games_fetch_pending;
            pthread_mutex_unlock(&backend_mutex);

            if (is_loading_games) {
                Vector2 center = { SCREEN_WIDTH / 2.0f, center_y };
                DrawRing(center, 40.0f, 50.0f, spinner_angle, spinner_angle + 90.0f, 64, COLOR_ACCENT);
                DrawRing(center, 40.0f, 50.0f, spinner_angle + 180.0f, spinner_angle + 270.0f, 64, COLOR_ACCENT);
                DrawText("Loading library...", SCREEN_WIDTH / 2 - MeasureText("Loading library...", 20) / 2, center_y + 80, 20, COLOR_TEXT_MUTED);
            } else if (image_count == 0) {
                DrawText("No games found in library.", SCREEN_WIDTH / 2 - MeasureText("No games found in library.", 30) / 2, center_y, 30, COLOR_TEXT_MUTED);
                DrawText("Check your backend server connection.", SCREEN_WIDTH / 2 - MeasureText("Check your backend server connection.", 20) / 2, center_y + 50, 20, Fade(COLOR_TEXT_MUTED, 0.6f));
            }

            for (int i = 0; i < image_count; i++) {
```

Wait, `image_count` might be set to 1 because of the procedural fallback:
```c
    // Procedural Fallback if empty
    if (image_count == 0) {
        image_names[0] = strdup("Library");
        image_count = 1;
    }
```
If `image_count` is 1 due to the procedural fallback, it will never be 0!
Ah! This fallback is run ONCE at initialization.
```c
    if (image_count == 0) {
        image_names[0] = strdup("Library");
        image_count = 1;
    }
```
Wait, let's see where that fallback is.
Wait, if `image_count` is 1 due to the fallback, then later `process_covers` updates it to `local_game_count`:
```c
        if (process_covers) {
            if (local_game_count > 0) {
                // ...
                image_count = local_game_count;
            }
        }
```
Wait! If `local_game_count == 0` (no games on the backend), `process_covers` does NOT update `image_count` because of `if (local_game_count > 0)`.
So it will remain at 1, showing the "Library" fallback indefinitely.
Is that the intended UX?

"The console UI expects game card metadata and covers to stream remotely from a backend server, using question-square.png as the permanent client fallback."
So the fallback is intentional. "permanent client fallback".

Okay, so maybe I shouldn't replace it with an empty state text.

What about "Add tooltips to icon-only buttons"?
Let's see if there's an icon-only button without a label.
The Topbar profile and settings buttons have NO text label! They are just icons.

Let's look at `topbar_selection == 0` and `topbar_selection == 1`.
```c
            // Topbar
            float topbar_y = 20;
            if (active_user_index < user_count) {
                // Profile Avatar (Top Left)
                Rectangle profile_rect = { 40, topbar_y, 40, 40 };
                if (topbar_selection == 0) {
                    DrawRectangleRounded((Rectangle){36, topbar_y-4, 48, 48}, 0.5f, 16, COLOR_ACCENT);
                }
                // ...
            }

            // Top Right Icons
            float right_start = SCREEN_WIDTH - 60;
            if (topbar_selection == 1) {
                DrawRectangleRounded((Rectangle){right_start-4, topbar_y-4, 48, 48}, 0.5f, 16, COLOR_ACCENT);
            }
```
If we add a tooltip when they are focused:
```c
                if (topbar_selection == 0) {
                    DrawRectangleRounded((Rectangle){36, topbar_y-4, 48, 48}, 0.5f, 16, COLOR_ACCENT);
                    DrawRectangleRounded((Rectangle){36, topbar_y+54, MeasureText("Switch Profile", 20) + 20, 30}, 0.5f, 16, (Color){20, 25, 30, 240});
                    DrawText("Switch Profile", 46, topbar_y+60, 20, COLOR_TEXT_MAIN);
                }
```
And for Settings:
```c
            if (topbar_selection == 1) {
                DrawRectangleRounded((Rectangle){right_start-4, topbar_y-4, 48, 48}, 0.5f, 16, COLOR_ACCENT);
                int tw = MeasureText("System Settings", 20);
                DrawRectangleRounded((Rectangle){right_start - tw + 40, topbar_y+54, tw + 20, 30}, 0.5f, 16, (Color){20, 25, 30, 240});
                DrawText("System Settings", right_start - tw + 50, topbar_y+60, 20, COLOR_TEXT_MAIN);
            }
```

Is there anything else?
What about the dock? The dock has text labels already:
```c
                    DrawText(dock_items[i], dock_x + 20, dock_y + 15, 20, COLOR_TEXT_MAIN);
```

Wait, what if the user doesn't know what the buttons mean when they're NOT focused?
What if we add text next to it or just rely on the tooltip when focused? In console UIs, it's very common to only show tooltip text when the icon is focused.

Let's rethink: "Missing Loading States for async operations".
Wait! `games_fetch_pending` is TRUE initially. If it's fetching, `image_count == 1` and it just shows the fallback. It doesn't tell the user that it's still loading. Adding a loading indicator in the corner or near the fallback would be very helpful.

But maybe tooltips are a very clean and simple addition!
Let's see:
```c
            if (active_user_index < user_count) {
                // Profile Avatar (Top Left)
                Rectangle profile_rect = { 40, topbar_y, 40, 40 };
                if (topbar_selection == 0) {
                    DrawRectangleRounded((Rectangle){36, topbar_y-4, 48, 48}, 0.5f, 16, COLOR_ACCENT);
                    DrawRectangleRounded((Rectangle){36, topbar_y+54, MeasureText("Switch Profile", 20) + 20, 30}, 0.5f, 16, (Color){20, 25, 30, 240});
                    DrawText("Switch Profile", 46, topbar_y+59, 20, COLOR_TEXT_MAIN);
                }
                if (tex_user.id > 0) {
                    DrawTexturePro(tex_user, (Rectangle){0, 0, tex_user.width, tex_user.height}, profile_rect, (Vector2){0,0}, 0.0f, WHITE);
                } else {
                    DrawRectangleRounded(profile_rect, 0.5f, 16, COLOR_TEXT_MUTED);
                }
            }

            // Top Right Icons
            float right_start = SCREEN_WIDTH - 60;
            if (topbar_selection == 1) {
                DrawRectangleRounded((Rectangle){right_start-4, topbar_y-4, 48, 48}, 0.5f, 16, COLOR_ACCENT);
                int tw = MeasureText("System Settings", 20);
                DrawRectangleRounded((Rectangle){right_start - tw + 20, topbar_y+54, tw + 20, 30}, 0.5f, 16, (Color){20, 25, 30, 240});
                DrawText("System Settings", right_start - tw + 30, topbar_y+59, 20, COLOR_TEXT_MAIN);
            }
            if (tex_settings.id > 0) {
                DrawTexturePro(tex_settings, (Rectangle){0, 0, tex_settings.width, tex_settings.height}, (Rectangle){right_start, topbar_y, 40, 40}, (Vector2){0,0}, 0.0f, WHITE);
            } else {
                DrawRectangleRounded((Rectangle){right_start, topbar_y, 40, 40}, 0.5f, 16, COLOR_TEXT_MUTED);
            }
```
Wait, let me double check the memory once again.
"UI animations and scrolling (e.g., horizontal carousels) should use smooth easing interpolation to center the active element rather than hard-snapping."
Does THIS repo have hard snapping right now?
Let's check `card_y_offsets`.
`card_y_offsets[i] += (target_y - card_y_offsets[i]) * 15.0f * dt;`
This uses easing.
What about `camera_offset_x`?
`camera_offset_x += (target_camera_x - camera_offset_x) * 15.0f * dt;`
This uses easing.

Is there ANY other carousel?
The profile selection:
```c
            int y = SCREEN_HEIGHT / 2 - 100;

            for (int i=0; i<user_count; i++) {
                int x = start_x + i * (card_w + spacing);
                Rectangle rect = { x, y, card_w, card_w };
```
Wait! The profile selection doesn't scroll at all! It just hardcodes `start_x = (SCREEN_WIDTH - total_w) / 2;`
What if the memory means the profile selection?
No, the memory specifically says `(e.g., horizontal carousels)`.
Wait, what if `STATE_DASHBOARD` was literally what the user was talking about, and it ALREADY uses smooth easing because someone else (or a previous commit) implemented it?
Or maybe `camera_offset_x` shouldn't use Euler integration `val += (target - val) * speed * dt`, but a TRUE easing function?
No, Euler integration for camera follow is standard and perfectly fine for a simple Raylib appliance.

Okay, let's implement the tooltip tooltips! It perfectly fits:
"Missing tooltips for icon-only buttons"

Let's do a quick compile test with the tooltips.
Wait, my previous tooltip code didn't match the new UI structure! The Topbar layout changed, and the tooltips I wrote were injected into the wrong place and with incorrect bounding box assumptions.
In `src/main.c`, the Settings Button is drawn:
```c
            // Settings Button
            Rectangle settings_rect = { right_anchor - 40, 20, 40, 40 };
            right_anchor -= 60;
            if (topbar_selection == 1) {
                DrawRectangleRounded(settings_rect, 0.15f, 16, COLOR_CARD_FOCUS);
                DrawRectangleRoundedLinesEx(settings_rect, 0.15f, 16, 2.0f, COLOR_ACCENT);
                // TOOLTIP for Settings
                int tw = MeasureText("System Settings", 20);
                DrawRectangleRounded((Rectangle){settings_rect.x + 20 - tw/2 - 10, settings_rect.y + 50, tw + 20, 30}, 0.15f, 16, (Color){20, 25, 30, 240});
                DrawText("System Settings", settings_rect.x + 20 - tw/2, settings_rect.y + 55, 20, COLOR_TEXT_MAIN);
            }
```
And for the Profile Button:
```c
            // Profile Button
            Rectangle profile_rect = { right_anchor - 40, 20, 40, 40 };
            right_anchor -= 60;
            if (topbar_selection == 0) {
                DrawRectangleRounded(profile_rect, 0.15f, 16, COLOR_CARD_FOCUS);
                DrawRectangleRoundedLinesEx(profile_rect, 0.15f, 16, 2.0f, COLOR_ACCENT);
                // TOOLTIP for Profile
                int tw = MeasureText("Switch Profile", 20);
                DrawRectangleRounded((Rectangle){profile_rect.x + 20 - tw/2 - 10, profile_rect.y + 50, tw + 20, 30}, 0.15f, 16, (Color){20, 25, 30, 240});
                DrawText("Switch Profile", profile_rect.x + 20 - tw/2, profile_rect.y + 55, 20, COLOR_TEXT_MAIN);
            }
```

This perfectly solves the "Missing tooltips for icon-only buttons" UX improvement. Wait! In the current UI, if the profile button has no text, the user sees `username_disp` right next to it!
Wait!
```c
            if (tex_user.id > 0) {
                DrawTextureEx(tex_user, (Vector2){ profile_rect.x + 8, profile_rect.y + 8 }, 0.0f, 24.0f / tex_user.width, WHITE);
            } else {
                DrawText(username_disp, profile_rect.x - MeasureText(username_disp, 20) - 10, profile_rect.y + 10, 20, COLOR_TEXT_MAIN);
                DrawText("P", profile_rect.x + 12, profile_rect.y + 10, 20, COLOR_TEXT_MAIN);
            }
```
If `tex_user.id > 0`, it draws the image, but DOES NOT draw the `username_disp`. So it's an icon-only button when the avatar exists! A tooltip is highly appropriate here. For the Settings button, it's ALWAYS an icon-only button unless `tex_settings.id == 0`, in which case it's just "S" which also needs a tooltip!

Let's do a patch for these tooltips.
## 2024-05-23 - Topbar Tooltips implementation
**Learning:** Added tooltips under topbar icons (Settings, Profile). Used simple bounded rectangle logic (`tt_x + tw + 20 > SCREEN_WIDTH` check).
**Action:** Tooltips work without issue, compiling successfully.
=======
## 2024-05-20 - Icon-Only Button Tooltips
**Learning:** Icon-only buttons without tooltips hide context, especially in TV/Gamepad UIs where users can't easily rely on traditional mouse hover text. Users with a fallback avatar (e.g. "P") might not realize it stands for Profile.
**Action:** Always add a clear, focused tooltip when an icon-only button is active to improve accessibility and provide immediate context.