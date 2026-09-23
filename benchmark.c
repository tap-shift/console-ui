#include <raylib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int main() {
    InitWindow(800, 600, "Benchmark");

    char* names[64];
    int widths[64];
    for (int i = 0; i < 64; i++) {
        names[i] = "Sample Game Title For Testing";
        widths[i] = MeasureText(names[i], 32);
    }

    double start, end;
    long iterations = 100000;

    start = GetTime();
    volatile int total_width_1 = 0;
    for (long i = 0; i < iterations; i++) {
        for (int j = 0; j < 64; j++) {
            total_width_1 += MeasureText(names[j], 32);
        }
    }
    end = GetTime();
    double time_measure_text = end - start;

    start = GetTime();
    volatile int total_width_2 = 0;
    for (long i = 0; i < iterations; i++) {
        for (int j = 0; j < 64; j++) {
            total_width_2 += widths[j];
        }
    }
    end = GetTime();
    double time_cached = end - start;

    printf("MeasureText time: %f s\n", time_measure_text);
    printf("Cached time: %f s\n", time_cached);
    printf("Improvement: %.2fx faster\n", time_measure_text / time_cached);

    CloseWindow();
    return 0;
}
