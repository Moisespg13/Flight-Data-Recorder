import re

c_file = r'C:\Users\yo\Documents\ESP\FDR\FDR_Master\components\sd_storage\sd_storage.c'
with open(c_file, 'r', encoding='utf-8') as f:
    content = f.read()

ensure_space_logic = r'''
static void free_space_on_sd(const char *mount_point, bool *sd_ok_flag) {
    if (!*sd_ok_flag) return;

    uint64_t total_bytes = 0, free_bytes = 0;
    if (esp_vfs_fat_info(mount_point, &total_bytes, &free_bytes) != ESP_OK) {
        ESP_LOGE(TAG, "Error esp_vfs_fat_info en %s", mount_point);
        return;
    }

    const uint64_t MIN_FREE_BYTES = 50 * 1024 * 1024; // 50 MB

    if (free_bytes > MIN_FREE_BYTES) {
        return; // Espacio suficiente
    }

    ESP_LOGW(TAG, "Espacio bajo en %s (%llu MB). Buscando archivos viejos...", mount_point, free_bytes / (1024*1024));

    DIR *dir = opendir(mount_point);
    if (!dir) return;

    struct dirent *ent;
    char oldest_file[128] = "";
    uint32_t min_idx = 0xFFFFFFFF;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "FDR_", 4) == 0 && strstr(ent->d_name, ".bin")) {
            uint32_t idx = 0;
            if (sscanf(ent->d_name, "FDR_%lu.bin", &idx) == 1) {
                if (idx < min_idx) {
                    min_idx = idx;
                    snprintf(oldest_file, sizeof(oldest_file), "%s/%s", mount_point, ent->d_name);
                }
            }
        }
    }
    closedir(dir);

    if (oldest_file[0] != '\0') {
        ESP_LOGI(TAG, "Borrando archivo antiguo para liberar espacio: %s", oldest_file);
        unlink(oldest_file);
    }
}

void sd_storage_ensure_free_space(void) {
    free_space_on_sd(MOUNT_POINT_1, &sd1_ok);
    free_space_on_sd(MOUNT_POINT_2, &sd2_ok);
}
'''

content = re.sub(r'static void free_space_on_sd.*?\}', ensure_space_logic.strip(), content, flags=re.DOTALL)
content = content.replace('#include <sys/statvfs.h>\n', '')

with open(c_file, 'w', encoding='utf-8') as f:
    f.write(content)

print("Done")
