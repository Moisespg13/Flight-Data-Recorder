import re

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

c = c.replace('gpio_pullup_en(PIN_SD_MOSI);', '// gpio_pullup_en(PIN_SD_MOSI);')
c = c.replace('gpio_pullup_en(PIN_SD_MISO);', '// gpio_pullup_en(PIN_SD_MISO);')

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("MOSI/MISO pullups removed in prod.")
