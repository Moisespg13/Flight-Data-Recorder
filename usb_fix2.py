import re

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/usb_transfer/usb_transfer.c', 'r', encoding='utf-8') as f:
    c = f.read()

deinit_patch = '''  // Desmontar pendrive si estǭ montado
  unmount_pendrive();

  // Detener tarea USB de eventos
  if (s_usb_task_handle != NULL) {
    vTaskDelete(s_usb_task_handle);
    s_usb_task_handle = NULL;
  }
'''

c = re.sub(r'  // Desmontar pendrive si est.*?ESP_LOGI\(TAG, "USB Host cerrado"\);', deinit_patch + '\n  ESP_LOGI(TAG, "USB Host cerrado");', c, flags=re.DOTALL)

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/usb_transfer/usb_transfer.c', 'w', encoding='utf-8') as f:
    f.write(c)

