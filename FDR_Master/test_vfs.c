#include <stdio.h>
#include "esp_vfs_fat.h"
void test() {
    uint64_t total = 0, free = 0;
    esp_vfs_fat_info("/sd1", &total, &free);
}
