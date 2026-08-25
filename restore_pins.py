import os

paths = [
    'C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/master_config/include/pin_config.h',
    'C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/master_config/include/pin_config.h'
]

for path in paths:
    with open(path, 'r', encoding='utf-8') as f:
        c = f.read()
    
    c = c.replace('#define PIN_SD_MOSI 39', '#define PIN_SD_MOSI 38')
    c = c.replace('#define PIN_SD_CLK  40', '#define PIN_SD_CLK  39')
    c = c.replace('#define PIN_SD_MISO 42', '#define PIN_SD_MISO 40')
    c = c.replace('#define PIN_SD_CS1  38', '#define PIN_SD_CS1  1')
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(c)

print("Pins restored to MOSI=38, CLK=39, MISO=40, CS1=1, CS2=2")
