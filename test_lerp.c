#include <stdio.h>
#include <math.h>

float LerpSmooth(float start, float end, float amount) {
    // Basic linear interpolation, but if we do this every frame with distance it's equivalent to an exponential decay easing.
    return start + amount * (end - start);
}

int main() {
    float current = 0.0f;
    float target = 100.0f;
    float dt = 0.016f;
    for(int i=0; i<30; i++) {
        current = LerpSmooth(current, target, 15.0f * dt);
        printf("Frame %d: %f\n", i, current);
    }
    return 0;
}
