import threading
from collections import deque
import serial
import time
import numpy as np
import matplotlib
import csv
import atexit

matplotlib.use('TkAgg')  # Fix para Windows
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ================= CONFIGURACIÓN =================
SERIAL_PORT = "COM12"  # <--- ASEGURA QUE ESTE ES TU PUERTO
BAUDRATE = 115200
SAMPLE_RATE = 360
WINDOW_SECONDS = 5
LOOKBACK = 300   # <--- Variable que he ido tocando (v inicial 80)

MAX_POINTS = int(SAMPLE_RATE * WINDOW_SECONDS)

# Buffers para la gráfica en vivo
y_data = deque([0] * MAX_POINTS, maxlen=MAX_POINTS)
y_beats = deque([np.nan] * MAX_POINTS, maxlen=MAX_POINTS)
x_data = np.linspace(0, WINDOW_SECONDS, MAX_POINTS)

running = True
data_lock = threading.Lock()

# ================= ARCHIVOS CSV =================
f_senal = open('ecg_senal_cruda.csv', 'w', newline='')
csv_senal = csv.writer(f_senal)
csv_senal.writerow(['Tiempo(s)', 'Valor_ADC'])

f_latidos = open('ecg_latidos_detectados.csv', 'w', newline='')
csv_latidos = csv.writer(f_latidos)
csv_latidos.writerow(['Tiempo(s)', 'Amplitud', 'C0', 'C1', 'C2', 'C3', 'C4', 'C5', 'Clase'])


# Asegurar que los archivos se cierran al salir
def cerrar_archivos():
    f_senal.close()
    f_latidos.close()
    print("Archivos CSV guardados correctamente.")


atexit.register(cerrar_archivos)


def serial_reader():
    global running
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
        ser.reset_input_buffer()
        print(f"--- CONECTADO A {SERIAL_PORT} ---")
        print("Grabando datos en CSV... (Cierra la ventana gráfica para guardar y salir)")
    except Exception as e:
        print(f"Error abriendo puerto: {e}")
        return

    tiempo_inicio = None

    while running:
        try:
            line = ser.readline().decode(errors='ignore').strip()

            if line.startswith("COEFFS"):
                parts = line.split(',')
                if len(parts) > 1 and tiempo_inicio is not None:
                    coeffs = parts[1:-1]  # Los coeficientes C
                    clase_str = parts[-1].split(':')[1]  # Sacar el '0' o '1' de 'CLASE:0'

                    with data_lock:
                        buffer_list = list(y_data)
                        if len(buffer_list) > LOOKBACK:
                            segmento = buffer_list[-LOOKBACK:]
                            pico_real = min(segmento)   # <--- Valor cambiado (max)
                            idx_relativo = segmento.index(pico_real)
                            idx_correccion = -LOOKBACK + idx_relativo
                            y_beats[idx_correccion] = pico_real

                            # Calcular en qué segundo exacto ocurrió el pico
                            tiempo_actual = time.time() - tiempo_inicio
                            tiempo_pico = tiempo_actual - (LOOKBACK / SAMPLE_RATE)

                            # Escribir en CSV
                            fila_latido = [f"{tiempo_pico:.3f}", pico_real] + coeffs + [clase_str]
                            csv_latidos.writerow(fila_latido)
                            f_latidos.flush()

            else:
                try:
                    val = int(line.split(',')[0])
                    if tiempo_inicio is None:
                        tiempo_inicio = time.time()

                    tiempo_actual = time.time() - tiempo_inicio

                    with data_lock:
                        y_data.append(val)
                        y_beats.append(np.nan)

                    # Guardar la señal en el CSV
                    csv_senal.writerow([f"{tiempo_actual:.3f}", val])

                except ValueError:
                    pass

        except Exception as e:
            pass


t = threading.Thread(target=serial_reader, daemon=True)
t.start()

# ================= GRÁFICA EN VIVO =================
fig, ax = plt.subplots(figsize=(12, 6))
ax.set_title(f"ECG + Grabación en CSV")
ax.set_xlabel("Tiempo en ventana (s)")
ax.set_ylabel("ADC Value")
ax.grid(True, alpha=0.3)
ax.set_xlim(0, WINDOW_SECONDS)

line, = ax.plot([], [], color='#007acc', lw=1.0, label='Señal ECG')
scatter, = ax.plot([], [], 'ro', markersize=8, zorder=5, label='Latido')
ax.legend(loc='upper right')


def update(frame):
    if not running: plt.close(); return line, scatter
    with data_lock:
        curr_y = np.array(y_data)
        curr_b = np.array(y_beats)
    line.set_data(x_data, curr_y)
    mask = ~np.isnan(curr_b)
    scatter.set_data(x_data[mask], curr_b[mask])

    if len(curr_y) > 0:
        valid = curr_y[curr_y > 100]
        if len(valid) > 0:
            vmin, vmax = np.min(valid), np.max(valid)
            margin = (vmax - vmin) * 0.2
            ax.set_ylim(vmin - margin, vmax + margin)
    return line, scatter


print("Abriendo ventana gráfica...")
ani = animation.FuncAnimation(fig, update, interval=30, blit=False, cache_frame_data=False)
plt.show()
running = False
print("Cerrando conexión...")