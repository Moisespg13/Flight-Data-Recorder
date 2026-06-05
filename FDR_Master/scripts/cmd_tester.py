import serial
import struct
import sys
import threading
import time

CMD_HEADER = 0xAA55

CMD_READ_PARAM = 0x0001
CMD_WRITE_FORMAT = 0x0002
CMD_READ_FORMAT = 0x0003

CMD_ACK_OK = 0x0001
CMD_ACK_ERROR = 0xFFFF

def receive_thread(ser):
    buffer = bytearray()
    while True:
        try:
            data = ser.read(1)
            if data:
                buffer.extend(data)
                
                # Check if we have at least 6 bytes (size of our struct)
                if len(buffer) >= 6:
                    # Look for the header (0xAA55 little endian -> 0x55, 0xAA)
                    idx = -1
                    for i in range(len(buffer) - 5):
                        if buffer[i] == 0x55 and buffer[i+1] == 0xAA:
                            idx = i
                            break
                    
                    if idx != -1:
                        # Print any ASCII text that came BEFORE the packet
                        if idx > 0:
                            ascii_text = buffer[:idx].decode('ascii', errors='ignore')
                            if ascii_text.strip():
                                print(f"\n[ESP32 LOG]: {ascii_text.strip()}")
                        
                        # Extract the 6-byte packet
                        packet = buffer[idx:idx+6]
                        del buffer[:idx+6] # Remove processed bytes
                        
                        # Unpack using little-endian (<), 3 unsigned shorts (HHH)
                        header, command, data_val = struct.unpack('<HHH', packet)
                        
                        if command == CMD_READ_PARAM:
                            if data_val == CMD_ACK_ERROR:
                                sys.stdout.write(f"\r[Canal] ERROR (Inválido)                           ")
                            else:
                                sys.stdout.write(f"\r[Canal] Valor: {data_val}                              ")
                            sys.stdout.flush()
                        else:
                            print(f"\n--- RESPUESTA RECIBIDA ---")
                            print(f"Header:  0x{header:04X}")
                            print(f"Comando: 0x{command:04X}")
                            if command == CMD_WRITE_FORMAT:
                                if data_val == CMD_ACK_OK:
                                    print(f"Dato:    OK (Formato guardado)")
                                else:
                                    print(f"Dato:    ERROR")
                            elif command == CMD_READ_FORMAT:
                                print(f"Dato:    Formato actual es {data_val}")
                            else:
                                print(f"Dato:    0x{data_val:04X}")
                            print(f"--------------------------\n")
                    else:
                        # No header found, if buffer is getting large, print it as ASCII and clear it
                        if len(buffer) > 128:
                            # Keep the last byte in case it's 0x55 (first half of header)
                            last_byte = buffer[-1:]
                            ascii_text = buffer[:-1].decode('ascii', errors='ignore')
                            if ascii_text.strip():
                                print(f"[ESP32 LOG]: {ascii_text.strip()}", end='')
                            buffer = last_byte

        except Exception as e:
            print(f"Error reading from serial: {e}")
            break

def send_command(ser, command, data, verbose=True):
    # Pack using little-endian (<), 3 unsigned shorts (HHH)
    packet = struct.pack('<HHH', CMD_HEADER, command, data)
    ser.write(packet)
    if verbose:
        print(f"\n>> Enviado comando 0x{command:04X} con dato {data}")

def main():
    if len(sys.argv) < 2:
        print("Uso: python cmd_tester.py <COM_PORT>")
        print("Ejemplo: python cmd_tester.py COM4")
        sys.exit(1)

    port = sys.argv[1]
    
    # IMPORTANTE: Usamos 115200 baudios porque estamos compartiendo
    # el puerto de consola (UART0) para no usar un adaptador extra.
    baudrate = 115200

    try:
        ser = serial.Serial(port, baudrate, timeout=0.1)
        print(f"Conectado a {port} a {baudrate} baudios.")
    except Exception as e:
        print(f"Error abriendo el puerto {port}: {e}")
        print("Asegurate de que el monitor de ESP-IDF (idf.py monitor) esté CERRADO.")
        sys.exit(1)

    # Start receive thread
    t = threading.Thread(target=receive_thread, args=(ser,), daemon=True)
    t.start()

    time.sleep(1) # Give it a second to clear any startup logs

    while True:
        print("\n--- MENU PUERTO DE COMANDOS ---")
        print("1. Leer valor de un canal (CMD_READ_PARAM)")
        print("2. Escribir formato CEV en memoria (CMD_WRITE_FORMAT)")
        print("3. Leer formato CEV actual (CMD_READ_FORMAT)")
        print("4. Formatear MicroSD (CMD_FORMAT_SD)")
        print("0. Salir")
        
        opcion = input("Elige una opción: ")

        if opcion == '1':
            try:
                ch = int(input("Ingresá el número de canal (0 a 47): "))
                print("\n[MODO TIEMPO REAL] Presioná Ctrl+C para volver al menú principal...")
                try:
                    while True:
                        send_command(ser, CMD_READ_PARAM, ch, verbose=False)
                        time.sleep(0.5) # Actualización 2 veces por segundo (2 Hz)
                except KeyboardInterrupt:
                    print("\nSaliendo del modo tiempo real...")
            except ValueError:
                print("Entrada inválida.")
        elif opcion == '2':
            try:
                fmt = int(input("Ingresá el número de formato (1, 2 o 3): "))
                send_command(ser, CMD_WRITE_FORMAT, fmt)
            except ValueError:
                print("Entrada inválida.")
        elif opcion == '3':
            send_command(ser, CMD_READ_FORMAT, 0)
        elif opcion == '4':
            print("Atención: Esto borrará todos los archivos FDR en la MicroSD.")
            confirm = input("¿Estás seguro? Escribí 'SI' para continuar: ")
            if confirm == "SI":
                CMD_FORMAT_SD = 0x0004
                # 0x55AA (21930 en decimal) es la clave de seguridad requerida
                send_command(ser, CMD_FORMAT_SD, 0x55AA)
            else:
                print("Formateo cancelado.")
        elif opcion == '0':
            break
        else:
            print("Opción inválida.")
        
        time.sleep(0.5)

    ser.close()
    print("Desconectado.")

if __name__ == '__main__':
    main()
