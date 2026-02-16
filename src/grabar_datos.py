import serial
import csv
import time
import os

# CONFIGURACIÓN
SERIAL_PORT = "COM12"  # <--- REVISA QUE SEA TU PUERTO
BAUDRATE = 115200
FILENAME = "datos_ecg.csv"

# 1. PREGUNTAR QUÉ VAMOS A GRABAR
print("\n" + "=" * 40)
print("   GRABADOR DE DATOS ECG PARA IA   ")
print("=" * 40)
print("Primero grabaremos latidos NORMALES (quieto).")
print("Luego reinicia y graba latidos RUIDO (movimiento).")
print("-" * 40)
etiqueta = input("¿Qué vas a grabar ahora? (Escribe 0 para Normal, 1 para Ruido): ")

if etiqueta not in ['0', '1']:
    print("Error: Solo escribe 0 o 1.")
    exit()

print(f"\n>>> PREPARADO PARA GRABAR CLASE {etiqueta} <<<")
print("Conecta la placa. Cuando salgan datos, déjalo correr 60 segundos.")
print("Pulsa Ctrl + C para parar y guardar.\n")

# Abrir archivo (modo 'a' es append, añadir al final sin borrar lo anterior)
# Si no existe, creamos cabeceras
file_exists = os.path.isfile(FILENAME)

try:
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
    ser.reset_input_buffer()
except Exception as e:
    print(f"Error puerto: {e}")
    exit()

# Abrimos el CSV
f = open(FILENAME, 'a', newline='')
writer = csv.writer(f)

if not file_exists:
    writer.writerow(["c0", "c1", "c2", "c3", "c4", "c5", "label"])

count = 0

try:
    while True:
        try:
            line = ser.readline().decode(errors='ignore').strip()

            # Solo nos interesan las líneas que dicen "COEFFS"
            if line.startswith("COEFFS"):
                parts = line.split(',')
                # Ejemplo: COEFFS,1200,300...
                if len(parts) >= 7:  # COEFFS + 6 numeros
                    coeffs = parts[1:7]  # Cogemos los 6 numeros

                    # Guardamos en el archivo
                    row = coeffs + [etiqueta]
                    writer.writerow(row)
                    f.flush()  # <--- ESTO OBLIGA A GUARDAR EN EL DISCO YA

                    count += 1
                    print(f"[{count}] Guardado (Clase {etiqueta}): {coeffs}")

        except UnicodeDecodeError:
            pass
except KeyboardInterrupt:
    print("\n\nGrabación detenida por usuario.")

f.close()
ser.close()
print(f"--- FIN. Se guardaron {count} muestras en {FILENAME} ---")