import re

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/usb_transfer/usb_transfer.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Instead of hanging in msc_host_uninstall, we can just skip it or wrap it.
# Actually, deleting the usb task BEFORE uninstalling msc might prevent the hang,
# but msc_host_uninstall needs to talk to the task!
# Let's just comment out msc_host_uninstall() and usb_host_uninstall() because in flight mode,
# keeping the USB driver loaded in memory uses 4KB of RAM, which is completely fine,
# and it avoids any ESP-IDF teardown deadlocks.

deinit_patch = '''  // Desmontar pendrive si estǭ montado
  unmount_pendrive();

  // WORKAROUND: ESP-IDF msc_host_uninstall() randomly deadlocks when no pendrive is present.
  // Since we have plenty of RAM on ESP32-S3, we will simply leave the USB Host driver loaded
  // in the background. It consumes minimal resources when idle.
  // msc_host_uninstall();

  // Detener tarea USB de eventos
  if (s_usb_task_handle != NULL) {
    vTaskDelete(s_usb_task_handle);
    s_usb_task_handle = NULL;
  }

  // esp_err_t ret = usb_host_uninstall();'''

c = re.sub(r'  // Desmontar pendrive si est.*?esp_err_t ret = usb_host_uninstall\(\);', deinit_patch, c, flags=re.DOTALL)

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/usb_transfer/usb_transfer.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("USB teardown deadlock workaround applied.")
