#include <stdio.h>
#include <cjson/cJSON.h>

int main() {
    const char *json_string = "{\"storage_pools\": [{\"total_bytes\": 10000000000, \"free_bytes\": 5000000000}]}";
    cJSON *json = cJSON_Parse(json_string);
    if (json) {
        cJSON *pools = cJSON_GetObjectItem(json, "storage_pools");
        if (pools && cJSON_IsArray(pools)) {
            cJSON *pool = cJSON_GetArrayItem(pools, 0);
            if (pool) {
                cJSON *total = cJSON_GetObjectItem(pool, "total_bytes");
                cJSON *free = cJSON_GetObjectItem(pool, "free_bytes");
                if (total) printf("Total: %f\n", total->valuedouble);
                if (free) printf("Free: %f\n", free->valuedouble);
            }
        }
    }
    return 0;
}
