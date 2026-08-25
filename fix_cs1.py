import os

paths = [
    'C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/master_config/include/pin_config.h',
    'C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/master_config/include/pin_config.h'
]

for path in paths:
    with open(path, 'r', encoding='utf-8') as f:
        c = f.read()
    
    c = c.replace('#define PIN_SD_CS1  1', '#define PIN_SD_CS1  38')
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(c)

print("PIN_SD_CS1 set to 38.")
