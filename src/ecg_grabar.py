import threading
from collections import deque
import serial
import numpy as np
import matplotlib
import csv
import atexit

matplotlib.use('TkAgg')  # Fix para Windows
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ================= CONFIGURACIÓN =================
SERIAL_PORT = "COM7"  # <--- TU PUERTO ACTUAL
BAUDRATE = 115200
SAMPLE_RATE = 360
WINDOW_SECONDS = 5
LOOKBACK = 80

MAX_POINTS = int(SAMPLE_RATE * WINDOW_SECONDS)

# Buffers
t_data = deque([0.0] * MAX_POINTS, maxlen=MAX_POINTS)
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
        print("Esperando datos y grabando... (Cierra la gráfica para guardar los CSV)")
    except Exception as e:
        print(f"Error abriendo puerto: {e}")
        return

    sample_count = 0  # <--- NUEVO: Contador infalible de muestras

    while running:
        try:
            line = ser.readline().decode(errors='ignore').strip()

            # CASO 1: Recibimos los Coeficientes (Latido Detectado)
            if line.startswith("COEFFS"):
                parts = line.split(',')
                if len(parts) > 1 and sample_count > 0:
                    coeffs = parts[1:-1]
                    clase_str = "0"
                    if "CLASE:" in parts[-1]:
                        clase_str = parts[-1].split(':')[1]

                    print(f"\n>>> ❤ LATIDO PROCESADO | Hermite: {coeffs} | Clase: {clase_str}")

                    with data_lock:
                        buffer_y = list(y_data)
                        buffer_t = list(t_data)

                        if len(buffer_y) > LOOKBACK:
                            segmento_y = buffer_y[-LOOKBACK:]
                            segmento_t = buffer_t[-LOOKBACK:]

                            pico_real = max(segmento_y)
                            idx_relativo = segmento_y.index(pico_real)

                            idx_correccion = -LOOKBACK + idx_relativo
                            y_beats[idx_correccion] = pico_real

                            tiempo_pico = segmento_t[idx_relativo]

                            # Guardamos con 4 decimales para mayor precisión
                            fila_latido = [f"{tiempo_pico:.4f}", pico_real] + coeffs + [clase_str]
                            csv_latidos.writerow(fila_latido)
                            f_latidos.flush()

            # CASO 2: Recibimos señal normal
            else:
                try:
                    val = int(line.split(',')[0])

                    # --- LA MAGIA ESTÁ AQUÍ ---
                    sample_count += 1
                    tiempo_actual = sample_count / SAMPLE_RATE  # Tiempo matemático perfecto

                    with data_lock:
                        t_data.append(tiempo_actual)
                        y_data.append(val)
                        y_beats.append(np.nan)

                    csv_senal.writerow([f"{tiempo_actual:.4f}", val])

                except ValueError:
                    pass

        except Exception as e:
            pass

t = threading.Thread(target=serial_reader, daemon=True)
t.start()

# ================= GRÁFICA =================
fig, ax = plt.subplots(figsize=(12, 6))
ax.set_title(f"ECG + Análisis Hermite en Tiempo Real (GRABANDO)")
ax.set_xlabel("Tiempo (s)")
ax.set_ylabel("ADC Value")
ax.grid(True, alpha=0.3)

ax.set_xlim(0, WINDOW_SECONDS)

line, = ax.plot([], [], color='#007acc', lw=1.0, label='Señal ECG')
scatter, = ax.plot([], [], 'ro', markersize=8, zorder=5, label='Latido (Hermite Calc)')

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
print("Cerrando conexión.")