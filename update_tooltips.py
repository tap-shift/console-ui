import re

with open('src/main.c', 'r') as f:
    content = f.read()

# Replace settings button
settings_replacement = """            if (topbar_selection == 1) {
                DrawRectangleRounded(settings_rect, 0.15f, 16, COLOR_CARD_FOCUS);
                DrawRectangleRoundedLinesEx(settings_rect, 0.15f, 16, 2.0f, COLOR_ACCENT);
                int tw = MeasureText("System Settings", 20);
                float tt_x = settings_rect.x + 20 - tw / 2.0f;
                if (tt_x + tw + 20 > SCREEN_WIDTH) tt_x = SCREEN_WIDTH - tw - 20;
                DrawRectangleRounded((Rectangle){tt_x - 10, settings_rect.y + 54, tw + 20, 30}, 0.15f, 16, (Color){20, 25, 30, 240});
                DrawText("System Settings", tt_x, settings_rect.y + 59, 20, COLOR_TEXT_MAIN);
            }"""

content = re.sub(r'            if \(topbar_selection == 1\) \{\n                DrawRectangleRounded\(settings_rect, 0.15f, 16, COLOR_CARD_FOCUS\);\n                DrawRectangleRoundedLinesEx\(settings_rect, 0.15f, 16, 2.0f, COLOR_ACCENT\);\n            \}', settings_replacement, content)


# Replace profile button
profile_replacement = """            if (topbar_selection == 0) {
                DrawRectangleRounded(profile_rect, 0.15f, 16, COLOR_CARD_FOCUS);
                DrawRectangleRoundedLinesEx(profile_rect, 0.15f, 16, 2.0f, COLOR_ACCENT);
                int tw = MeasureText("Switch Profile", 20);
                float tt_x = profile_rect.x + 20 - tw / 2.0f;
                if (tt_x + tw + 20 > SCREEN_WIDTH) tt_x = SCREEN_WIDTH - tw - 20;
                DrawRectangleRounded((Rectangle){tt_x - 10, profile_rect.y + 54, tw + 20, 30}, 0.15f, 16, (Color){20, 25, 30, 240});
                DrawText("Switch Profile", tt_x, profile_rect.y + 59, 20, COLOR_TEXT_MAIN);
            }"""

content = re.sub(r'            if \(topbar_selection == 0\) \{\n                DrawRectangleRounded\(profile_rect, 0.15f, 16, COLOR_CARD_FOCUS\);\n                DrawRectangleRoundedLinesEx\(profile_rect, 0.15f, 16, 2.0f, COLOR_ACCENT\);\n            \}', profile_replacement, content)

with open('src/main.c', 'w') as f:
    f.write(content)
