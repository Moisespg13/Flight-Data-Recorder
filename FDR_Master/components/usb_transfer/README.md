# usb_transfer

Modo descarga. USB-OTG del ESP32-S3 en Host, clase MSC.

Al arrancar, `main` espera unos segundos un pendrive. Si aparece, no se inicia la adquisición: se montan las MicroSD y se copian los `FDR_*.bin` al pendrive. Si no aparece, se cierra el host USB y sigue el modo vuelo.

D− es GPIO 19 y D+ es GPIO 20. Durante el vuelo este puerto no se inicializa.
