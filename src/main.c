#define _POSIX_C_SOURCE 200809L
#include <raylib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

void GetConfigDir(char* out_path, size_t max_len) {
    const char* home = getenv("HOME");
    if (home) snprintf(out_path, max_len, "%s/.config/console-ui", home);
    else snprintf(out_path, max_len, "/tmp/console-ui");
}

void GetCacheDir(char* out_path, size_t max_len) {
    const char* home = getenv("HOME");
    if (home) snprintf(out_path, max_len, "%s/.cache/console-ui", home);
    else snprintf(out_path, max_len, "/tmp/cache/console-ui");
}

void GetDataDir(char* out_path, size_t max_len) {
    const char* home = getenv("HOME");
    if (home) snprintf(out_path, max_len, "%s/.local/share/console-ui", home);
    else snprintf(out_path, max_len, "/tmp/share/console-ui");
}

void MakeDirs() {
    char cmd[512];
    char p[256];
    GetConfigDir(p, sizeof(p)); snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", p); int ret = system(cmd); (void)ret;
    GetCacheDir(p, sizeof(p)); snprintf(cmd, sizeof(cmd), "mkdir -p \"%s/covers\" \"%s/avatars\"", p, p); ret = system(cmd); (void)ret;
    GetDataDir(p, sizeof(p)); snprintf(cmd, sizeof(cmd), "mkdir -p \"%s/saves\"", p); ret = system(cmd); (void)ret;
}


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

Texture2D tex_user = {0};
Texture2D tex_settings = {0};
Texture2D tex_bell = {0};
Texture2D tex_pulse = {0};
Texture2D tex_music = {0};
Texture2D tex_globe = {0};
Texture2D tex_clock = {0};

typedef struct {
    char id[64];
    char title[128];
    char launch_path[256];
    char cover_url[256];
    char banner_url[256];
    char version[32];
    char category[64];
    char save_subpath[128];
    bool cover_downloaded;
    bool cover_failed;
} Game;

typedef struct {
    char game_id[64];
} SaveSyncData;

#define MAX_GAMES 64
Game games[MAX_GAMES];
int game_count = 0;

bool system_connected = false;
long long total_bytes = 0;
long long free_bytes = 0;
char profile_username[128] = "";
bool cover_download_pending = false;
bool avatar_download_pending = false;

pthread_mutex_t backend_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef enum {
    STATE_DASHBOARD,
    STATE_UPDATING,
    STATE_SETTINGS,
    STATE_CONTROLLER_TEST,
    STATE_PROFILE_SELECT,
    STATE_INGAME
} AppUIState;

AppUIState current_state = STATE_DASHBOARD;

typedef struct {
    char id[64];
    char username[128];
    char avatar_url[256];
} User;

#define MAX_USERS 16
User users[MAX_USERS];
int user_count = 0;
char active_user_id[64] = "";
int active_user_index = 0;
bool users_fetch_pending = true;
bool games_fetch_pending = true;
bool avatar_fetch_pending = false;
char avatar_download_url[256] = "";
bool avatar_download_success = false;
bool avatar_download_failed = false;


typedef enum {
    PROFILE_PS5 = 0,
    PROFILE_XBOX,
    PROFILE_PS2_LEGACY
} ControllerProfile;

ControllerProfile active_profile = PROFILE_PS5;

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

bool game_running = false;
bool game_paused = false;
pid_t active_game_pid = -1;
char active_game_id[64] = "";
int overlay_selection_global = 0;

void LoadSettings() {
    char config_dir[256];
    GetConfigDir(config_dir, sizeof(config_dir));
    char settings_path[512];
    snprintf(settings_path, sizeof(settings_path), "%s/settings.json", config_dir);
    FILE *fp = fopen(settings_path, "r");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *string = malloc((size_t)fsize + 1);
        if (string && fread(string, 1, (size_t)fsize, fp) == (size_t)fsize) {
            string[fsize] = 0;
            cJSON *json = cJSON_Parse(string);
            if (json) {
                cJSON *bgm = cJSON_GetObjectItem(json, "bgm_muted");
                if (cJSON_IsBool(bgm)) bgm_muted = cJSON_IsTrue(bgm);
                cJSON *audio = cJSON_GetObjectItem(json, "active_audio_device");
                if (cJSON_IsNumber(audio)) active_audio_device = audio->valueint;
                cJSON *prof = cJSON_GetObjectItem(json, "active_profile");
                if (cJSON_IsNumber(prof)) active_profile = (ControllerProfile)prof->valueint;
                cJSON *uid = cJSON_GetObjectItem(json, "last_active_user");
                if (cJSON_IsString(uid)) strncpy(active_user_id, uid->valuestring, sizeof(active_user_id)-1);
                cJSON_Delete(json);
            }
        }
        if (string) free(string);
        fclose(fp);
    }
}

void SaveSettings() {
    char config_dir[256];
    GetConfigDir(config_dir, sizeof(config_dir));
    char settings_path[512];
    snprintf(settings_path, sizeof(settings_path), "%s/settings.json", config_dir);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "bgm_muted", bgm_muted);
    cJSON_AddNumberToObject(json, "active_audio_device", active_audio_device);
    cJSON_AddNumberToObject(json, "active_profile", (int)active_profile);
    cJSON_AddStringToObject(json, "last_active_user", active_user_id);
    char *string = cJSON_Print(json);
    FILE *fp = fopen(settings_path, "w");
    if (fp) {
        fputs(string, fp);
        fclose(fp);
    }
    cJSON_Delete(json);
    if (string) free(string);
}


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

struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(!ptr) return 0;

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}


void* SaveSyncThread(void* arg) {
    SaveSyncData* data = (SaveSyncData*)arg;
    char game_id[64];
    strncpy(game_id, data->game_id, sizeof(game_id)-1);
    game_id[63] = '\0';
    free(data);

    char save_dir[512];
    char cache_dir[256];
    GetDataDir(cache_dir, sizeof(cache_dir));

    pthread_mutex_lock(&backend_mutex);
    char u_id[64];
    strncpy(u_id, active_user_id, sizeof(u_id)-1);
    u_id[63] = '\0';
    pthread_mutex_unlock(&backend_mutex);

    snprintf(save_dir, sizeof(save_dir), "%s/saves/%s/%s", cache_dir, u_id, game_id);

    char cmd[512];

    if (access(save_dir, F_OK) == 0) {
        // Tar the save dir
        char tar_path[512];
        snprintf(tar_path, sizeof(tar_path), "/tmp/save_%s.tar.gz", game_id);
        snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" .", tar_path, save_dir);
        int ret = system(cmd);
        (void)ret;

        // Upload
        CURL *curl = curl_easy_init();
        if (curl) {
            char url[256] = "http://192.168.222.181:8080/api/v1/saves/sync";
            curl_mime *form = curl_mime_init(curl);
            curl_mimepart *field;

            field = curl_mime_addpart(form);
            curl_mime_name(field, "game_id");
            curl_mime_data(field, game_id, CURL_ZERO_TERMINATED);

            field = curl_mime_addpart(form);
            curl_mime_name(field, "user_id");
            curl_mime_data(field, u_id, CURL_ZERO_TERMINATED);

            field = curl_mime_addpart(form);
            curl_mime_name(field, "file");
            curl_mime_filedata(field, tar_path);

            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
            curl_easy_perform(curl);

            curl_mime_free(form);
            curl_easy_cleanup(curl);
        }

        unlink(tar_path);
    }

    return NULL;
}

void* BackendWorkerThread(void* arg) {
    (void)arg;
    CURL *curl;
    CURLcode res;

    char base_url[128] = "http://192.168.222.181:8080";

    // Test primary endpoint, fallback if needed
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.222.181:8080/api/v1/system/status");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
        if (curl_easy_perform(curl) != CURLE_OK) {
            strcpy(base_url, "http://TowerServer.local:8080");
        }
        curl_easy_cleanup(curl);
    }

    while(1) {
        if (users_fetch_pending) {
            users_fetch_pending = false;
            curl = curl_easy_init();
            if(curl) {
                struct MemoryStruct chunk;
                chunk.memory = malloc(1);
                chunk.size = 0;

                char url[256];
                snprintf(url, sizeof(url), "%s/api/v1/users", base_url);
                curl_easy_setopt(curl, CURLOPT_URL, url);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

                res = curl_easy_perform(curl);
                if(res == CURLE_OK) {
                    long response_code;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                    if (response_code == 200) {
                        cJSON *json = cJSON_Parse(chunk.memory);
                        if (json != NULL && cJSON_IsArray(json)) {
                            int num_users = cJSON_GetArraySize(json);
                            if (num_users > MAX_USERS) num_users = MAX_USERS;
                            pthread_mutex_lock(&backend_mutex);
                            user_count = num_users;
                            for (int i = 0; i < num_users; i++) {
                                cJSON *item = cJSON_GetArrayItem(json, i);
                                cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
                                cJSON *username = cJSON_GetObjectItemCaseSensitive(item, "username");
                                cJSON *avatar = cJSON_GetObjectItemCaseSensitive(item, "avatar_url");
                                if (cJSON_IsString(id)) strncpy(users[i].id, id->valuestring, sizeof(users[i].id)-1);
                                if (cJSON_IsString(username)) strncpy(users[i].username, username->valuestring, sizeof(users[i].username)-1);
                                if (cJSON_IsString(avatar)) {
                                    if (avatar->valuestring[0] == '/') {
                                        snprintf(users[i].avatar_url, sizeof(users[i].avatar_url), "http://192.168.222.181:8080%s", avatar->valuestring);
                                    } else {
                                        snprintf(users[i].avatar_url, sizeof(users[i].avatar_url), "%s%s", base_url, avatar->valuestring);
                                    }
                                }
                            }
                            pthread_mutex_unlock(&backend_mutex);
                            cJSON_Delete(json);
                        }
                    }
                }
                free(chunk.memory);
                curl_easy_cleanup(curl);
            }
        }

        if (games_fetch_pending && strlen(active_user_id) > 0) {
            games_fetch_pending = false;
            curl = curl_easy_init();
            if (curl) {
                struct MemoryStruct chunk;
                chunk.memory = malloc(1);
                chunk.size = 0;

                char url[256];
                snprintf(url, sizeof(url), "%s/api/v1/games?user=%s", base_url, active_user_id);
                curl_easy_setopt(curl, CURLOPT_URL, url);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

                res = curl_easy_perform(curl);
                if (res == CURLE_OK) {
                    long response_code;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                    if (response_code == 200) {
                        cJSON *json = cJSON_Parse(chunk.memory);
                        if (json != NULL && cJSON_IsArray(json)) {
                            int num_games = cJSON_GetArraySize(json);
                            if (num_games > MAX_GAMES) num_games = MAX_GAMES;

                            pthread_mutex_lock(&backend_mutex);
                            game_count = num_games;

                            for (int i = 0; i < num_games; i++) {
                                cJSON *item = cJSON_GetArrayItem(json, i);
                                cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
                                cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
                                cJSON *launch_path = cJSON_GetObjectItemCaseSensitive(item, "launch_path");
                                cJSON *cover_url = cJSON_GetObjectItemCaseSensitive(item, "cover_url");

                                if (cJSON_IsString(id)) strncpy(games[i].id, id->valuestring, sizeof(games[i].id) - 1);
                                if (cJSON_IsString(title)) strncpy(games[i].title, title->valuestring, sizeof(games[i].title) - 1);
                                if (cJSON_IsString(launch_path)) strncpy(games[i].launch_path, launch_path->valuestring, sizeof(games[i].launch_path) - 1);
                                if (cJSON_IsString(cover_url)) {
                                    strncpy(games[i].cover_url, cover_url->valuestring, sizeof(games[i].cover_url) - 1);
                                }
                            }
                            pthread_mutex_unlock(&backend_mutex);

                            for (int i = 0; i < num_games; i++) {
                                cJSON *item = cJSON_GetArrayItem(json, i);
                                cJSON *cover_url = cJSON_GetObjectItemCaseSensitive(item, "cover_url");
                                if (cJSON_IsString(cover_url)) {

                                    char cover_full_url[512];
                                    if (cover_url->valuestring[0] == '/') {
                                        snprintf(cover_full_url, sizeof(cover_full_url), "http://192.168.222.181:8080%s", cover_url->valuestring);
                                    } else {
                                        snprintf(cover_full_url, sizeof(cover_full_url), "%s%s", base_url, cover_url->valuestring);
                                    }

                                    char cache_dir[256]; GetCacheDir(cache_dir, sizeof(cache_dir));

                                    char local_path[512];
                                    snprintf(local_path, sizeof(local_path), "%s/covers/%s.png", cache_dir, games[i].id);

                                    games[i].cover_downloaded = false;
                                    games[i].cover_failed = false;

                                    FILE *fp = fopen(local_path, "wb");
                                    if (fp) {
                                        CURL *curl_dl = curl_easy_init();
                                        if (curl_dl) {
                                            curl_easy_setopt(curl_dl, CURLOPT_URL, cover_full_url);
                                            curl_easy_setopt(curl_dl, CURLOPT_WRITEFUNCTION, NULL);
                                            curl_easy_setopt(curl_dl, CURLOPT_WRITEDATA, fp);
                                            CURLcode dl_res = curl_easy_perform(curl_dl);
                                            long response_code = 0;
                                            curl_easy_getinfo(curl_dl, CURLINFO_RESPONSE_CODE, &response_code);
                                            if (dl_res == CURLE_OK && response_code == 200) {
                                                pthread_mutex_lock(&backend_mutex);
                                                games[i].cover_downloaded = true;
                                                pthread_mutex_unlock(&backend_mutex);
                                            } else {
                                                pthread_mutex_lock(&backend_mutex);
                                                games[i].cover_failed = true;
                                                pthread_mutex_unlock(&backend_mutex);
                                            }
                                            curl_easy_cleanup(curl_dl);
                                        } else {
                                            pthread_mutex_lock(&backend_mutex);
                                            games[i].cover_failed = true;
                                            pthread_mutex_unlock(&backend_mutex);
                                        }
                                        fclose(fp);
                                        pthread_mutex_lock(&backend_mutex);
                                        if (games[i].cover_failed) {
                                            unlink(local_path);
                                        }
                                        pthread_mutex_unlock(&backend_mutex);
                                    } else {
                                        pthread_mutex_lock(&backend_mutex);
                                        games[i].cover_failed = true;
                                        pthread_mutex_unlock(&backend_mutex);
                                    }
                                }
                            }
                            cJSON_Delete(json);
                            pthread_mutex_lock(&backend_mutex);
                            cover_download_pending = true;
                            pthread_mutex_unlock(&backend_mutex);
                        }
                    }
                }
                if (chunk.memory) free(chunk.memory);
                if (curl) curl_easy_cleanup(curl);
            }
        }

        if (avatar_fetch_pending && strlen(avatar_download_url) > 0) {
            avatar_fetch_pending = false;
            char cache_dir[256]; GetCacheDir(cache_dir, sizeof(cache_dir));

            char local_path[512];
            snprintf(local_path, sizeof(local_path), "%s/avatars/%s.png", cache_dir, active_user_id);
            avatar_download_success = false;
            avatar_download_failed = false;

            FILE *fp = fopen(local_path, "wb");
            if (fp) {
                 CURL *curl_dl = curl_easy_init();
                 if (curl_dl) {
                     curl_easy_setopt(curl_dl, CURLOPT_URL, avatar_download_url);
                     curl_easy_setopt(curl_dl, CURLOPT_WRITEFUNCTION, NULL);
                     curl_easy_setopt(curl_dl, CURLOPT_WRITEDATA, fp);
                     CURLcode dl_res = curl_easy_perform(curl_dl);
                     long response_code = 0;
                     curl_easy_getinfo(curl_dl, CURLINFO_RESPONSE_CODE, &response_code);
                     if (dl_res == CURLE_OK && response_code == 200) {
                         pthread_mutex_lock(&backend_mutex);
                         avatar_download_success = true;
                         pthread_mutex_unlock(&backend_mutex);
                     } else {
                         pthread_mutex_lock(&backend_mutex);
                         avatar_download_failed = true;
                         pthread_mutex_unlock(&backend_mutex);
                     }
                     curl_easy_cleanup(curl_dl);
                 } else {
                     pthread_mutex_lock(&backend_mutex);
                     avatar_download_failed = true;
                     pthread_mutex_unlock(&backend_mutex);
                 }
                 fclose(fp);
                 pthread_mutex_lock(&backend_mutex);
                 if (avatar_download_failed) {
                     unlink(local_path);
                 }
                 pthread_mutex_unlock(&backend_mutex);

                 pthread_mutex_lock(&backend_mutex);
                 avatar_download_pending = true;
                 pthread_mutex_unlock(&backend_mutex);
            } else {
                pthread_mutex_lock(&backend_mutex);
                avatar_download_failed = true;
                pthread_mutex_unlock(&backend_mutex);
            }
        }

        curl = curl_easy_init();
        if(curl) {
            struct MemoryStruct chunk;
            chunk.memory = malloc(1);
            chunk.size = 0;

            char url[256];
            snprintf(url, sizeof(url), "%s/api/v1/system/status", base_url);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);

            res = curl_easy_perform(curl);

            bool connected = false;
            long long tb = 0, fb = 0;

            if(res == CURLE_OK) {
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                if (response_code == 200) {
                    cJSON *json = cJSON_Parse(chunk.memory);
                    if (json != NULL) {
                        connected = true;
                        cJSON *pools = cJSON_GetObjectItemCaseSensitive(json, "storage_pools");
                        if (cJSON_IsArray(pools)) {
                            cJSON *pool = cJSON_GetArrayItem(pools, 0);
                            if (pool != NULL) {
                                cJSON *total = cJSON_GetObjectItemCaseSensitive(pool, "total_bytes");
                                cJSON *free_b = cJSON_GetObjectItemCaseSensitive(pool, "free_bytes");
                                if (cJSON_IsNumber(total) && cJSON_IsNumber(free_b)) {
                                    tb = (long long)total->valuedouble;
                                    fb = (long long)free_b->valuedouble;
                                }
                            }
                        }
                        cJSON_Delete(json);
                    }
                }
            }

            pthread_mutex_lock(&backend_mutex);
            system_connected = connected;
            if (connected) {
                total_bytes = tb;
                free_bytes = fb;
            }
            pthread_mutex_unlock(&backend_mutex);

            free(chunk.memory);
            curl_easy_cleanup(curl);
        }
        sleep(2);
    }
    return NULL;
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
    MakeDirs();
    LoadSettings();
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

    if (FileExists("assets/images/user.png")) { tex_user = LoadTexture("assets/images/user.png"); SetTextureFilter(tex_user, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/images/settings-sliders.png")) { tex_settings = LoadTexture("assets/images/settings-sliders.png"); SetTextureFilter(tex_settings, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/images/bell.png")) { tex_bell = LoadTexture("assets/images/bell.png"); SetTextureFilter(tex_bell, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/images/pulse.png")) { tex_pulse = LoadTexture("assets/images/pulse.png"); SetTextureFilter(tex_pulse, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/images/music-alt.png")) { tex_music = LoadTexture("assets/images/music-alt.png"); SetTextureFilter(tex_music, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/images/globe.png")) { tex_globe = LoadTexture("assets/images/globe.png"); SetTextureFilter(tex_globe, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/images/clock.png")) { tex_clock = LoadTexture("assets/images/clock.png"); SetTextureFilter(tex_clock, TEXTURE_FILTER_BILINEAR); }

    if (FileExists("assets/images/question-square.png")) {
        tex_icons[0] = LoadTexture("assets/images/question-square.png");
        SetTextureFilter(tex_icons[0], TEXTURE_FILTER_BILINEAR);
        image_names[0] = strdup("Library");
        image_count = 1;
    }

    // Procedural Fallback if empty
    if (image_count == 0) {
        image_names[0] = strdup("Library");
        image_count = 1;
    }

    SetExitKey(KEY_NULL);

    int current_selection = 0;
    int dock_selection = -1; // -1 means focus is on the shelf
    int topbar_selection = -1; // -1 means not focused, 0: Profile, 1: Settings
    bool should_close = false;
    bool show_profile_dropdown = false;
    int dropdown_selection = 0;
    float spinner_angle = 0.0f;
    float camera_offset_x = 0.0f;
    float bg_fade = 0.0f;
    int last_selection = 0;
    Texture2D last_bg_tex = {0};

    pthread_t checker_thread;
    pthread_create(&checker_thread, NULL, UpdateCheckerThread, NULL);

    pthread_t backend_thread;
    pthread_create(&backend_thread, NULL, BackendWorkerThread, NULL);

    static int active_gamepad = 0;

    while (!WindowShouldClose() && !should_close) {
        if (active_game_pid > 0) {
            int status;
            pid_t p = waitpid(active_game_pid, &status, WNOHANG);
            if (p > 0) {
                // Game terminated
                active_game_pid = -1;
                game_running = false;
                game_paused = false;

                SaveSyncData* sync_data = malloc(sizeof(SaveSyncData));
                if (sync_data) {
                    strncpy(sync_data->game_id, active_game_id, sizeof(sync_data->game_id)-1);
                    sync_data->game_id[63] = '\0';
                    pthread_t sync_thread;
                    pthread_create(&sync_thread, NULL, SaveSyncThread, sync_data);
                    pthread_detach(sync_thread);
                }

                pthread_mutex_lock(&update_mutex);
                current_state = STATE_DASHBOARD;
                pthread_mutex_unlock(&update_mutex);
            }
        }

        if (bgm.stream.buffer != NULL) UpdateMusicStream(bgm);

        float dt = GetFrameTime();

        static bool was_running = false;
        bool should_hide = game_running && !game_paused;
        if (should_hide && !was_running) {
            MinimizeWindow();
            was_running = true;
        }
        if (!should_hide && was_running) {
            RestoreWindow();
            was_running = false;
        }

        for (int i = 0; i < 4; i++) {
            if (IsGamepadAvailable(i)) {
                if (fabs(GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_X)) > 0.25f ||
                    fabs(GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_Y)) > 0.25f ||
                    GetGamepadButtonPressed() != KEY_NULL ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_LEFT_FACE_UP) ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_LEFT_FACE_DOWN) ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
                    IsGamepadButtonPressed(i, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || IsGamepadButtonPressed(i, GAMEPAD_BUTTON_RIGHT_FACE_UP) || IsGamepadButtonPressed(i, GAMEPAD_BUTTON_MIDDLE_RIGHT) || IsGamepadButtonPressed(i, GAMEPAD_BUTTON_LEFT_TRIGGER_1) || IsGamepadButtonPressed(i, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
                    active_gamepad = i;
                    break;
                } else if (!IsGamepadAvailable(active_gamepad)) {
                    active_gamepad = i;
                }
            }
        }

        pthread_mutex_lock(&notif_mutex);
        if (notify_sound_pending) {
            if (sound_notify.stream.buffer != NULL) PlaySound(sound_notify);
            notify_sound_pending = false;
        }
        pthread_mutex_unlock(&notif_mutex);

        bool process_covers = false;
        bool process_avatar = false;
        int local_game_count = 0;
        Game local_games[MAX_GAMES];
        char local_active_user_id[64];

        pthread_mutex_lock(&backend_mutex);
        if (cover_download_pending) {
            process_covers = true;
            local_game_count = game_count;
            for (int i = 0; i < game_count; i++) {
                local_games[i] = games[i];
            }
            cover_download_pending = false;
        }
        if (avatar_download_pending) {
            process_avatar = true;
            strncpy(local_active_user_id, active_user_id, sizeof(local_active_user_id)-1);
            local_active_user_id[sizeof(local_active_user_id)-1] = '\0';
            avatar_download_pending = false;
        }
        pthread_mutex_unlock(&backend_mutex);

        if (process_covers) {
            if (local_game_count > 0) {
                for (int i = 0; i < local_game_count; i++) {
                    char local_path[512];
                    char cache_dir[256]; GetCacheDir(cache_dir, sizeof(cache_dir)); snprintf(local_path, sizeof(local_path), "%s/covers/%s.png", cache_dir, local_games[i].id);
                    if (local_games[i].cover_downloaded) {
                        if (tex_icons[i].id == 0 || tex_icons[i].id == tex_icons[0].id) {
                            if (tex_icons[i].id > 0 && tex_icons[i].id != tex_icons[0].id) UnloadTexture(tex_icons[i]);
                            tex_icons[i] = LoadTexture(local_path);
                            SetTextureFilter(tex_icons[i], TEXTURE_FILTER_BILINEAR);
                        }
                    } else if (local_games[i].cover_failed && tex_icons[0].id > 0) {
                        if (tex_icons[i].id == 0) {
                            tex_icons[i] = tex_icons[0]; // fallback to question mark
                        }
                    }
                    if (image_names[i]) free(image_names[i]);
                    image_names[i] = strdup(local_games[i].title);
                }
                image_count = local_game_count;
            }
        }

        if (process_avatar) {
            char cache_dir[256]; GetCacheDir(cache_dir, sizeof(cache_dir));
            char avatar_path[512]; snprintf(avatar_path, sizeof(avatar_path), "%s/avatars/%s.png", cache_dir, local_active_user_id);
            bool avatar_success;
            pthread_mutex_lock(&backend_mutex);
            avatar_success = avatar_download_success;
            pthread_mutex_unlock(&backend_mutex);
            if (avatar_success) {
                if (tex_user.id > 0) UnloadTexture(tex_user);
                tex_user = LoadTexture(avatar_path);
                SetTextureFilter(tex_user, TEXTURE_FILTER_BILINEAR);
                pthread_mutex_lock(&backend_mutex);
                avatar_download_success = false;
                pthread_mutex_unlock(&backend_mutex);
            }
        }

        pthread_mutex_lock(&update_mutex);
        AppUIState state_copy = current_state;
        pthread_mutex_unlock(&update_mutex);

        if (state_copy == STATE_DASHBOARD) {
            if (users_fetch_pending == false && strlen(active_user_id) == 0 && user_count > 0) {
                pthread_mutex_lock(&update_mutex);
                current_state = STATE_PROFILE_SELECT;
                pthread_mutex_unlock(&update_mutex);
                continue;
            }
            bool move_left = IsKeyPressed(KEY_LEFT);
            bool move_right = IsKeyPressed(KEY_RIGHT);
            bool move_up = IsKeyPressed(KEY_UP);
            bool move_down = IsKeyPressed(KEY_DOWN);
            bool select = IsKeyPressed(KEY_ENTER);
            bool back = IsKeyPressed(KEY_ESCAPE);
            bool toggle_notif = IsKeyPressed(KEY_N) || IsKeyPressed(KEY_X);
            bool trigger_update = IsKeyPressed(KEY_U) || IsKeyPressed(KEY_Y);

            if (IsGamepadAvailable(active_gamepad)) {
                static double last_nav_time = 0;
                double current_time = GetTime();

                float axis_x = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_X);
                float axis_y = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_Y);
                if (fabs(axis_x) < 0.25f) axis_x = 0.0f; // Deadzone
                if (fabs(axis_y) < 0.25f) axis_y = 0.0f;

                if (active_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.25f && (current_time - last_nav_time > 0.3))) {
                        move_left = true;
                        if (axis_x < -0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.25f && (current_time - last_nav_time > 0.3))) {
                        move_right = true;
                        if (axis_x > 0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.25f && (current_time - last_nav_time > 0.3))) {
                        move_up = true;
                        if (axis_y < -0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP)) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.25f && (current_time - last_nav_time > 0.3))) {
                        move_down = true;
                        if (axis_y > 0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) last_nav_time = current_time;
                    }

                    if (IsGamepadButtonPressed(active_gamepad, 2)) select = true;
                    if (IsGamepadButtonPressed(active_gamepad, 1)) back = true;
                    if (IsGamepadButtonPressed(active_gamepad, 3)) toggle_notif = true;
                    if (IsGamepadButtonPressed(active_gamepad, 0)) trigger_update = true;
                } else {
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.25f && (current_time - last_nav_time > 0.3))) {
                        move_left = true;
                        if (axis_x < -0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.25f && (current_time - last_nav_time > 0.3))) {
                        move_right = true;
                        if (axis_x > 0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.25f && (current_time - last_nav_time > 0.3))) {
                        move_up = true;
                        if (axis_y < -0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP)) last_nav_time = current_time;
                    }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.25f && (current_time - last_nav_time > 0.3))) {
                        move_down = true;
                        if (axis_y > 0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) last_nav_time = current_time;
                    }

                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT)) select = true;
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) toggle_notif = true;
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_UP)) trigger_update = true;
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
                int prev_topbar = topbar_selection;

                if (move_down && dock_selection == -1 && topbar_selection == -1) {
                    dock_selection = 0; // Focus dock
                } else if (move_up && dock_selection != -1 && topbar_selection == -1) {
                    dock_selection = -1; // Focus shelf
                } else if (move_up && dock_selection == -1 && topbar_selection == -1) {
                    topbar_selection = 0; // Focus topbar
                } else if (move_down && topbar_selection != -1) {
                    topbar_selection = -1; // Focus shelf
                }

                if (dock_selection == -1 && topbar_selection == -1) {
                    if (move_left) {
                        current_selection--;
                        if (current_selection < 0) current_selection = 0;
                    }
                    if (move_right) {
                        current_selection++;
                        if (current_selection >= image_count) current_selection = image_count - 1;
                    }
                } else if (dock_selection != -1) {
                    if (move_left) {
                        dock_selection--;
                        if (dock_selection < 0) dock_selection = 0;
                    }
                    if (move_right) {
                        dock_selection++;
                        if (dock_selection > 3) dock_selection = 3;
                    }
                } else if (topbar_selection != -1) {
                    if (move_left) {
                        topbar_selection--;
                        if (topbar_selection < 0) topbar_selection = 0;
                    }
                    if (move_right) {
                        topbar_selection++;
                        if (topbar_selection > 1) topbar_selection = 1;
                    }
                }

                if (current_selection != prev_sel || dock_selection != prev_dock || topbar_selection != prev_topbar) {
                    if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
                }

                if (select) {
                    if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
                    if (dock_selection == -1 && topbar_selection == -1) {
                        if (current_selection < image_count) {
                            pthread_mutex_lock(&backend_mutex);
                            char exec_path[512];
                            if (current_selection < game_count) {
                                snprintf(exec_path, sizeof(exec_path), "%s", games[current_selection].launch_path);
                            } else {
                                exec_path[0] = '\0';
                            }
                            pthread_mutex_unlock(&backend_mutex);

                            if (strlen(exec_path) > 0) {
                                if (access(exec_path, X_OK) != 0) {
                                    AddNotification("Game not found or not executable");
                                } else {
                                    char save_dir[512];
                                    char cache_dir[256];
                                    GetDataDir(cache_dir, sizeof(cache_dir));

                                    pthread_mutex_lock(&backend_mutex);
                                    char u_id[64];
                                    strncpy(u_id, active_user_id, sizeof(u_id)-1);
                                    u_id[63] = '\0';
                                    pthread_mutex_unlock(&backend_mutex);

                                    snprintf(save_dir, sizeof(save_dir), "%s/saves/%s/%s", cache_dir, u_id, games[current_selection].id);

                                    fprintf(stderr, "Launching: %s\n", exec_path);
                                    game_running = true;
                                    game_paused = false;
                                    strncpy(active_game_id, games[current_selection].id, sizeof(active_game_id)-1);
                                    active_game_id[63] = '\0';

                                    pthread_mutex_lock(&update_mutex);
                                    current_state = STATE_INGAME;
                                    pthread_mutex_unlock(&update_mutex);

                                    pid_t pid = fork();
                                    if (pid == 0) {
                                        setenv("XDG_DATA_HOME", save_dir, 1);
                                        execl("/bin/sh", "sh", "-c", exec_path, (char *)NULL);
                                        exit(1);
                                    } else if (pid > 0) {
                                        active_game_pid = pid;
                                    } else {
                                        game_running = false;
                                        pthread_mutex_lock(&update_mutex);
                                        current_state = STATE_DASHBOARD;
                                        pthread_mutex_unlock(&update_mutex);
                                    }
                                }
                            } else {
                                printf("Selected fallback: %s\n", image_names[current_selection]);
                            }
                        }
                    } else if (dock_selection != -1) {
                        if (dock_selection == 1) { // Settings
                            pthread_mutex_lock(&update_mutex);
                            current_state = STATE_SETTINGS;
                            pthread_mutex_unlock(&update_mutex);
                        } else if (dock_selection == 3) { // Power
                            should_close = true;
                        }
                    } else if (topbar_selection != -1) {
                        if (topbar_selection == 1) { // Settings
                            pthread_mutex_lock(&update_mutex);
                            current_state = STATE_SETTINGS;
                            pthread_mutex_unlock(&update_mutex);
                        } else if (topbar_selection == 0) { // Profile
                            pthread_mutex_lock(&update_mutex);
                            current_state = STATE_PROFILE_SELECT;
                            pthread_mutex_unlock(&update_mutex);
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

            if (IsGamepadAvailable(active_gamepad)) {
                static double last_nav_time = 0;
                double current_time = GetTime();

                float axis_x = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_X);
                float axis_y = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_Y);
                if (fabs(axis_x) < 0.25f) axis_x = 0.0f; // Deadzone
                if (fabs(axis_y) < 0.25f) axis_y = 0.0f;

                if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (axis_x < -0.25f && (current_time - last_nav_time > 0.3))) {
                    move_left = true;
                    if (axis_x < -0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (axis_x > 0.25f && (current_time - last_nav_time > 0.3))) {
                    move_right = true;
                    if (axis_x > 0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.25f && (current_time - last_nav_time > 0.3))) {
                    move_up = true;
                    if (axis_y < -0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP)) last_nav_time = current_time;
                }
                if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.25f && (current_time - last_nav_time > 0.3))) {
                    move_down = true;
                    if (axis_y > 0.25f || IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) last_nav_time = current_time;
                }

                if (active_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(active_gamepad, 1)) back = true;
                    if (IsGamepadButtonPressed(active_gamepad, 2)) confirm = true;
                } else {
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) confirm = true;
                }
            }

            if (show_profile_dropdown) {
                if (move_up) {
                    dropdown_selection--;
                    if (dropdown_selection < 0) dropdown_selection = 0;
                    if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
                }
                if (move_down) {
                    dropdown_selection++;
                    if (dropdown_selection > 2) dropdown_selection = 2;
                    if (sound_nav.stream.buffer != NULL) PlaySound(sound_nav);
                }
                if (confirm) {
                    active_profile = (ControllerProfile)dropdown_selection;
                    show_profile_dropdown = false;
                    if (sound_select.stream.buffer != NULL) PlaySound(sound_select);
                }
                if (back) {
                    show_profile_dropdown = false;
                    if (sound_back.stream.buffer != NULL) PlaySound(sound_back);
                }
            } else {
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
                            int ret = system(cmd);
                            (void)ret;
                        }
                    } else if (settings_tab == 1 && settings_row == 0) {
                        show_profile_dropdown = true;
                        dropdown_selection = (int)active_profile;
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
            }
        } else if (state_copy == STATE_PROFILE_SELECT) {
            bool move_left = IsKeyPressed(KEY_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
            bool move_right = IsKeyPressed(KEY_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
            bool select = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

            if (move_left) {
                active_user_index--;
                if (active_user_index < 0) active_user_index = 0;
            }
            if (move_right) {
                active_user_index++;
                if (active_user_index >= user_count) active_user_index = user_count - 1;
            }
            if (select && user_count > 0) {
                pthread_mutex_lock(&backend_mutex);
                strncpy(active_user_id, users[active_user_index].id, sizeof(active_user_id)-1);
                strncpy(avatar_download_url, users[active_user_index].avatar_url, sizeof(avatar_download_url)-1);
                games_fetch_pending = true;
                avatar_fetch_pending = true;
                pthread_mutex_unlock(&backend_mutex);

                SaveSettings();

                pthread_mutex_lock(&update_mutex);
                current_state = STATE_DASHBOARD;
                pthread_mutex_unlock(&update_mutex);
            }
        } else if (state_copy == STATE_CONTROLLER_TEST) {
            bool back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B);

            if (IsGamepadAvailable(active_gamepad)) {
                if (active_profile == PROFILE_PS2_LEGACY) {
                    if (IsGamepadButtonPressed(active_gamepad, 1)) back = true;
                } else {
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) back = true;
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_MIDDLE)) back = true; // PS button / Guide
                }
            }

            if (back) {
                if (sound_back.stream.buffer != NULL) PlaySound(sound_back);
                pthread_mutex_lock(&update_mutex);
                current_state = STATE_SETTINGS;
                pthread_mutex_unlock(&update_mutex);
            }
        } else if (state_copy == STATE_INGAME) {
            if (!game_paused) {
                bool pause_req = false;
                if (IsGamepadAvailable(active_gamepad)) {
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_MIDDLE)) pause_req = true;
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) &&
                        IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) &&
                        IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT)) pause_req = true;
                }
                if (pause_req && active_game_pid > 0) {
                    kill(active_game_pid, SIGSTOP);
                    game_paused = true;
                    overlay_selection_global = 0;
                }
            } else {
                bool move_up = false;
                bool move_down = false;
                bool select = false;

                if (IsGamepadAvailable(active_gamepad)) {
                    static double last_nav_time = 0;
                    double current_time = GetTime();
                    float axis_y = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_Y);
                    if (fabs(axis_y) < 0.25f) axis_y = 0.0f;

                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_UP) || (axis_y < -0.25f && (current_time - last_nav_time > 0.3))) { move_up = true; last_nav_time = current_time; }
                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (axis_y > 0.25f && (current_time - last_nav_time > 0.3))) { move_down = true; last_nav_time = current_time; }

                    if (IsGamepadButtonPressed(active_gamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) select = true;
                    if (active_profile == PROFILE_PS2_LEGACY && IsGamepadButtonPressed(active_gamepad, 2)) select = true;
                }

                if (move_up) overlay_selection_global = 0;
                if (move_down) overlay_selection_global = 1;

                if (select) {
                    if (overlay_selection_global == 0) {
                        // Resume
                        if (active_game_pid > 0) kill(active_game_pid, SIGCONT);
                        game_paused = false;
                    } else if (overlay_selection_global == 1) {
                        // Exit Game
                        if (active_game_pid > 0) {
                            kill(active_game_pid, SIGTERM);
                            int status;
                            waitpid(active_game_pid, &status, 0);
                            active_game_pid = -1;
                        }
                        game_running = false;
                        game_paused = false;

                        SaveSyncData* sync_data = malloc(sizeof(SaveSyncData));
                        if (sync_data) {
                            strncpy(sync_data->game_id, active_game_id, sizeof(sync_data->game_id)-1);
                            sync_data->game_id[63] = '\0';
                            pthread_t sync_thread;
                            pthread_create(&sync_thread, NULL, SaveSyncThread, sync_data);
                            pthread_detach(sync_thread);
                        }

                        pthread_mutex_lock(&update_mutex);
                        current_state = STATE_DASHBOARD;
                        pthread_mutex_unlock(&update_mutex);
                    }
                }
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
            DrawText("Console UI", 40, 30, 24, COLOR_TEXT_MAIN);

            time_t t = time(NULL);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);

            int right_anchor = SCREEN_WIDTH - 40;

            // Settings Button
            Rectangle settings_rect = { right_anchor - 40, 20, 40, 40 };
            right_anchor -= 60;
            if (topbar_selection == 1) {
                DrawRectangleRounded(settings_rect, 0.15f, 16, COLOR_CARD_FOCUS);
                DrawRectangleRoundedLinesEx(settings_rect, 0.15f, 16, 2.0f, COLOR_ACCENT);

                int sw = MeasureText("Settings", 20);
                Rectangle tooltip_rect = { settings_rect.x + settings_rect.width / 2.0f - sw / 2.0f - 10, settings_rect.y + settings_rect.height + 10, sw + 20, 30 };
                DrawRectangleRounded(tooltip_rect, 0.25f, 16, COLOR_CARD_FOCUS);
                DrawText("Settings", tooltip_rect.x + 10, tooltip_rect.y + 5, 20, COLOR_TEXT_MAIN);
            }
            if (tex_settings.id > 0) {
                DrawTextureEx(tex_settings, (Vector2){ settings_rect.x + 8, settings_rect.y + 8 }, 0.0f, 24.0f / tex_settings.width, WHITE);
            } else {
                DrawText("S", settings_rect.x + 12, settings_rect.y + 10, 20, COLOR_TEXT_MAIN);
            }

            // Profile Button
            Rectangle profile_rect = { right_anchor - 40, 20, 40, 40 };
            right_anchor -= 60;

            pthread_mutex_lock(&backend_mutex);
            char username_disp[128];
            strncpy(username_disp, "Profile", sizeof(username_disp) - 1);
            username_disp[sizeof(username_disp) - 1] = '\0';
            for (int i=0; i<user_count; i++) {
                if (strcmp(users[i].id, active_user_id) == 0) {
                    strncpy(username_disp, users[i].username, 127);
                    username_disp[127] = '\0';
                    break;
                }
            }
            pthread_mutex_unlock(&backend_mutex);

            if (topbar_selection == 0) {
                DrawRectangleRounded(profile_rect, 0.15f, 16, COLOR_CARD_FOCUS);
                DrawRectangleRoundedLinesEx(profile_rect, 0.15f, 16, 2.0f, COLOR_ACCENT);

                int pw = MeasureText(username_disp, 20);
                Rectangle tooltip_rect = { profile_rect.x + profile_rect.width / 2.0f - pw / 2.0f - 10, profile_rect.y + profile_rect.height + 10, pw + 20, 30 };
                DrawRectangleRounded(tooltip_rect, 0.25f, 16, COLOR_CARD_FOCUS);
                DrawText(username_disp, tooltip_rect.x + 10, tooltip_rect.y + 5, 20, COLOR_TEXT_MAIN);
            }

            if (tex_user.id > 0) {
                DrawTextureEx(tex_user, (Vector2){ profile_rect.x + 8, profile_rect.y + 8 }, 0.0f, 24.0f / tex_user.width, WHITE);
            } else {
                DrawText(username_disp, profile_rect.x - MeasureText(username_disp, 20) - 10, profile_rect.y + 10, 20, COLOR_TEXT_MAIN);
                DrawText("P", profile_rect.x + 12, profile_rect.y + 10, 20, COLOR_TEXT_MAIN);
            }

            // Bell / Notifications
            pthread_mutex_lock(&notif_mutex);
            int unread = unread_notifications;
            pthread_mutex_unlock(&notif_mutex);
            right_anchor -= 30;
            if (tex_bell.id > 0) {
                DrawTextureEx(tex_bell, (Vector2){ right_anchor, 28 }, 0.0f, 24.0f / tex_bell.width, COLOR_TEXT_MAIN);
            } else {
                DrawText("Bell (X)", right_anchor, 32, 20, COLOR_TEXT_MAIN);
            }
            if (unread > 0) DrawCircle(right_anchor + 24, 32, 6, COLOR_ACCENT);
            right_anchor -= 20;

            // Network
            right_anchor -= 30;

            pthread_mutex_lock(&backend_mutex);
            bool is_connected = system_connected;
            pthread_mutex_unlock(&backend_mutex);

            if (is_connected) {
                float pulse = (sin(GetTime() * 4.0f) + 1.0f) / 2.0f;
                DrawCircle(right_anchor + 30, 32, 4 + pulse * 2, Fade(GREEN, pulse));
                DrawCircle(right_anchor + 30, 32, 4, GREEN);
            } else {
                DrawCircle(right_anchor + 30, 32, 4, COLOR_ERROR);
            }

            if (tex_globe.id > 0) {
                DrawTextureEx(tex_globe, (Vector2){ right_anchor, 28 }, 0.0f, 24.0f / tex_globe.width, COLOR_TEXT_MUTED);
            } else {
                DrawText("Wi-Fi", right_anchor, 32, 20, COLOR_TEXT_MUTED);
            }
            right_anchor -= 20;

            // Audio
            right_anchor -= 30;
            if (tex_music.id > 0) {
                DrawTextureEx(tex_music, (Vector2){ right_anchor, 28 }, 0.0f, 24.0f / tex_music.width, COLOR_ACCENT);
            } else {
                DrawText("[Audio]", right_anchor, 32, 20, COLOR_ACCENT);
            }
            right_anchor -= 20;

            // Clock
            int time_w = MeasureText(time_str, 24);
            right_anchor -= time_w + 10;
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
            if (active_profile == PROFILE_PS5) {
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

                const char* profile_names[] = { "Sony DualSense (PS5)", "Xbox / Standard", "Legacy DirectInput" };

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
            } else if (settings_tab == 2) { // System
                DrawText("System & Storage", right_x, right_y, 30, COLOR_TEXT_MAIN);
                right_y += 60;

                pthread_mutex_lock(&backend_mutex);
                long long tb = total_bytes;
                long long fb = free_bytes;
                pthread_mutex_unlock(&backend_mutex);

                double tb_gb = (double)tb / (1024.0 * 1024.0 * 1024.0);
                double fb_gb = (double)fb / (1024.0 * 1024.0 * 1024.0);
                double ub_gb = tb_gb - fb_gb;

                char storage_text[512];
                char tb_str[64], fb_str[64], ub_str[64];

                if (tb_gb >= 1000.0) snprintf(tb_str, sizeof(tb_str), "%.2f TB", tb_gb / 1024.0);
                else snprintf(tb_str, sizeof(tb_str), "%.1f GB", tb_gb);

                if (fb_gb >= 1000.0) snprintf(fb_str, sizeof(fb_str), "%.2f TB", fb_gb / 1024.0);
                else snprintf(fb_str, sizeof(fb_str), "%.1f GB", fb_gb);

                if (ub_gb >= 1000.0) snprintf(ub_str, sizeof(ub_str), "%.2f TB", ub_gb / 1024.0);
                else snprintf(ub_str, sizeof(ub_str), "%.1f GB", ub_gb);

                snprintf(storage_text, sizeof(storage_text), "Total Storage: %s\nUsed Storage: %s\nFree Storage: %s", tb_str, ub_str, fb_str);

                DrawText(storage_text, right_x, right_y, 24, COLOR_TEXT_MAIN);
            }

            const char* legend = "(Up/Down) Select Item   (Left/Right) Change Tab / Option   (A) Confirm   (B) Back";
            int lw = MeasureText(legend, 20);
            DrawText(legend, (SCREEN_WIDTH - lw) / 2, SCREEN_HEIGHT - 40, 20, COLOR_TEXT_MUTED);

            if (show_profile_dropdown) {
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(COLOR_BG, 0.8f));

                float modal_w = 400;
                float modal_h = 240;
                float modal_x = (SCREEN_WIDTH - modal_w) / 2.0f;
                float modal_y = (SCREEN_HEIGHT - modal_h) / 2.0f;

                DrawRectangleRounded((Rectangle){modal_x, modal_y, modal_w, modal_h}, 0.15f, 32, COLOR_CARD_IDLE);
                DrawRectangleRoundedLinesEx((Rectangle){modal_x, modal_y, modal_w, modal_h}, 0.15f, 32, 2.0f, COLOR_ACCENT);

                DrawText("Select Layout", modal_x + modal_w/2 - MeasureText("Select Layout", 24)/2, modal_y + 20, 24, COLOR_TEXT_MAIN);

                const char* dropdown_names[] = { "Sony DualSense (PS5)", "Xbox / Standard", "Legacy DirectInput" };

                for (int i = 0; i < 3; i++) {
                    float opt_y = modal_y + 80 + i * 40;
                    if (dropdown_selection == i) {
                        DrawRectangle(modal_x + 20, opt_y - 5, modal_w - 40, 30, COLOR_CARD_FOCUS);
                        DrawRectangle(modal_x + 20, opt_y - 5, 4, 30, COLOR_ACCENT);
                        DrawText(dropdown_names[i], modal_x + 40, opt_y, 20, COLOR_TEXT_MAIN);
                    } else {
                        DrawText(dropdown_names[i], modal_x + 40, opt_y, 20, COLOR_TEXT_MUTED);
                    }
                }
            }

        } else if (render_state == STATE_PROFILE_SELECT) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);
            DrawText("Who is playing?", SCREEN_WIDTH / 2 - MeasureText("Who is playing?", 40) / 2, 200, 40, COLOR_TEXT_MAIN);

            int card_w = 200;
            int spacing = 50;
            int total_w = user_count * card_w + (user_count - 1) * spacing;
            int start_x = SCREEN_WIDTH / 2 - total_w / 2;
            int y = SCREEN_HEIGHT / 2 - 100;

            for (int i=0; i<user_count; i++) {
                int x = start_x + i * (card_w + spacing);
                Rectangle rect = { x, y, card_w, card_w };
                if (i == active_user_index) {
                    DrawRectangleRounded(rect, 0.15f, 32, COLOR_CARD_FOCUS);
                    DrawRectangleRoundedLinesEx(rect, 0.15f, 32, 4.0f, COLOR_ACCENT);
                } else {
                    DrawRectangleRounded(rect, 0.15f, 32, COLOR_CARD_IDLE);
                }

                int tw = MeasureText(users[i].username, 24);
                DrawText(users[i].username, x + card_w/2 - tw/2, y + card_w + 20, 24, (i == active_user_index) ? COLOR_TEXT_MAIN : COLOR_TEXT_MUTED);
            }
        } else if (render_state == STATE_CONTROLLER_TEST) {
            DrawRectangle(0, 0, SCREEN_WIDTH, 60, COLOR_TOPBAR);
            DrawText("Controller Test Mode", 40, 20, 22, COLOR_TEXT_MAIN);

            float cx = SCREEN_WIDTH / 2.0f;
            float cy = SCREEN_HEIGHT / 2.0f;

            DrawRectangleRounded((Rectangle){cx - 400, cy - 300, 800, 600}, 0.15f, 32, COLOR_CARD_IDLE);
            DrawRectangleRoundedLinesEx((Rectangle){cx - 400, cy - 300, 800, 600}, 0.15f, 32, 2.0f, COLOR_ACCENT);

            const char* gp_name_orig = IsGamepadAvailable(active_gamepad) ? GetGamepadName(active_gamepad) : "No Gamepad Detected";
            int nw = MeasureText(gp_name_orig, 24);
            DrawText(gp_name_orig, cx - nw / 2, cy - 250, 24, COLOR_TEXT_MAIN);

            // Visual Button Test Overlay
            float bx = cx + 200;
            float by = cy + 50;
            float br = 20.0f;

            int btn_y = (active_profile == PROFILE_PS2_LEGACY) ? 0 : GAMEPAD_BUTTON_RIGHT_FACE_UP;
            int btn_x = (active_profile == PROFILE_PS2_LEGACY) ? 3 : GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
            int btn_a = (active_profile == PROFILE_PS2_LEGACY) ? 2 : GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
            int btn_b = (active_profile == PROFILE_PS2_LEGACY) ? 1 : GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;

            Color c_y = IsGamepadButtonDown(active_gamepad, btn_y) ? COLOR_ACCENT : COLOR_CARD_IDLE;
            Color c_x = IsGamepadButtonDown(active_gamepad, btn_x) ? COLOR_ACCENT : COLOR_CARD_IDLE;
            Color c_a = IsGamepadButtonDown(active_gamepad, btn_a) ? COLOR_ACCENT : COLOR_CARD_IDLE;
            Color c_b = IsGamepadButtonDown(active_gamepad, btn_b) ? COLOR_ACCENT : COLOR_CARD_IDLE;

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

            float ax = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_X);
            float ay = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_Y);
            if (fabs(ax) < 0.25f) ax = 0.0f;
            if (fabs(ay) < 0.25f) ay = 0.0f;

            DrawCircle(sx + ax * 60, sy + ay * 60, 10, COLOR_ACCENT);

            // Triggers
            float l2 = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_LEFT_TRIGGER);
            float r2 = GetGamepadAxisMovement(active_gamepad, GAMEPAD_AXIS_RIGHT_TRIGGER);
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
        } else if (render_state == STATE_INGAME) {
            if (game_paused) {
                // Dimmed background
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (Color){ 0, 0, 0, 200 });

                // Quick-Access Overlay
                float panel_w = 400;
                float panel_h = 300;
                float px = SCREEN_WIDTH / 2.0f - panel_w / 2.0f;
                float py = SCREEN_HEIGHT / 2.0f - panel_h / 2.0f;

                DrawRectangleRounded((Rectangle){ px, py, panel_w, panel_h }, 0.15f, 16, COLOR_CARD_IDLE);
                DrawRectangleRoundedLinesEx((Rectangle){ px, py, panel_w, panel_h }, 0.15f, 16, 2.0f, COLOR_TEXT_MUTED);

                const char* title = "Game Paused";
                int tw = MeasureText(title, 32);
                DrawText(title, px + panel_w/2 - tw/2, py + 30, 32, COLOR_TEXT_MAIN);

                const char* opt1 = "Resume";
                const char* opt2 = "Exit Game";
                int o1w = MeasureText(opt1, 24);
                int o2w = MeasureText(opt2, 24);

                Color c1 = (overlay_selection_global == 0) ? COLOR_ACCENT : COLOR_TEXT_MUTED;
                Color c2 = (overlay_selection_global == 1) ? COLOR_ACCENT : COLOR_TEXT_MUTED;

                DrawText(opt1, px + panel_w/2 - o1w/2, py + 120, 24, c1);
                DrawText(opt2, px + panel_w/2 - o2w/2, py + 180, 24, c2);

                if (overlay_selection_global == 0) {
                    DrawRectangleLines(px + panel_w/2 - o1w/2 - 10, py + 115, o1w + 20, 34, COLOR_ACCENT);
                } else if (overlay_selection_global == 1) {
                    DrawRectangleLines(px + panel_w/2 - o2w/2 - 10, py + 175, o2w + 20, 34, COLOR_ACCENT);
                }
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
