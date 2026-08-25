import re
import os

paths = [
    'C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/master_config/include/pin_config.h',
    'C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/master_config/include/pin_config.h'
]

for path in paths:
    with open(path, 'r', encoding='utf-8') as f:
        c = f.read()
    
    c = c.replace('#define PIN_SD_MOSI 38', '#define PIN_SD_MOSI 39')
    c = c.replace('#define PIN_SD_CLK  39', '#define PIN_SD_CLK  40')
    c = c.replace('#define PIN_SD_MISO 40', '#define PIN_SD_MISO 42')
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(c)

print("Pins corrected in both repositories.")
