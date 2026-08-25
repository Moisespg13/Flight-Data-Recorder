import re

with open('FDR_Master/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Replace the remaining old sd_storage_init
c = re.sub(r'esp_err_t sd_storage_init\(void\) \{.*?\}', '', c, flags=re.DOTALL, count=1)
# wait, actually let's see what is there
