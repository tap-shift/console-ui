#include <raylib.h>
#include <stdio.h>
#include <math.h>

void RunTest() {
    float camera_offset_x = 0.0f;
    int current_selection = 0;
    float dt = 0.016f; // approx 60fps
    float card_base_width = 340.0f;
    float spacing = 60.0f;

    // Simulate moving to selection 1
    current_selection = 1;
    for (int i = 0; i < 60; ++i) {
        float target_camera_x = -((current_selection * (card_base_width + spacing)) + (card_base_width / 2.0f));
        camera_offset_x += (target_camera_x - camera_offset_x) * 15.0f * dt;
        printf("Frame %d: camera_offset_x = %f\n", i, camera_offset_x);
    }
}

int main() {
    RunTest();
    return 0;
}
