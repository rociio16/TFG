import threading
from collections import deque
import serial
import time
import numpy as np
import matplotlib

matplotlib.use('TkAgg')  # Fix para Windows
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ================= CONFIGURACIÓN =================
SERIAL_PORT = "COM12"  # <--- ASEGURA QUE ESTE ES TU PUERTO
BAUDRATE = 115200
SAMPLE_RATE = 360
WINDOW_SECONDS = 5  # 5 segundos para ver bien el detalle
LOOKBACK = 80  # Muestras atrás para corregir la posición del pico

MAX_POINTS = int(SAMPLE_RATE * WINDOW_SECONDS)

# Buffers
y_data = deque([0] * MAX_POINTS, maxlen=MAX_POINTS)
y_beats = deque([np.nan] * MAX_POINTS, maxlen=MAX_POINTS)  # Puntos rojos
x_data = np.linspace(0, WINDOW_SECONDS, MAX_POINTS)

running = True
data_lock = threading.Lock()


def serial_reader():
    global running
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
        ser.reset_input_buffer()
        print(f"--- CONECTADO A {SERIAL_PORT} ---")
        print("Esperando datos... (Asegúrate de tener los electrodos puestos)")
    except Exception as e:
        print(f"Error abriendo puerto: {e}")
        return

    while running:
        try:
            # Leemos una línea del puerto serie
            line = ser.readline().decode(errors='ignore').strip()

            # CASO 1: Recibimos los Coeficientes de Hermite (Latido Detectado)
            if line.startswith("COEFFS"):
                parts = line.split(',')
                if len(parts) > 1:
                    coeffs = parts[1:]
                    print(f"\n>>> ❤ LATIDO PROCESADO | Hermite: {coeffs}")

                    # --- DIBUJAR PUNTO ROJO ---
                    with data_lock:
                        buffer_list = list(y_data)
                        if len(buffer_list) > LOOKBACK:
                            segmento = buffer_list[-LOOKBACK:]
                            pico_real = max(segmento)
                            idx_relativo = segmento.index(pico_real)
                            idx_correccion = -LOOKBACK + idx_relativo

                            # Marcamos el punto rojo
                            y_beats[idx_correccion] = pico_real

            # CASO 2: Recibimos señal normal (ej: "2048")
            else:
                try:
                    # Cogemos el número. Si por algún motivo tiene coma (ej: 2048,0), split lo arregla
                    val = int(line.split(',')[0])

                    with data_lock:
                        y_data.append(val)
                        y_beats.append(np.nan)  # Nada de latido en este instante
                except ValueError:
                    # Si llega texto como ">>> NORMAL (Clase 0)" lo ignoramos en silencio
                    pass

        except Exception as e:
            pass


t = threading.Thread(target=serial_reader, daemon=True)
t.start()

# ================= GRÁFICA =================
fig, ax = plt.subplots(figsize=(12, 6))
ax.set_title(f"ECG + Análisis Hermite en Tiempo Real")
ax.set_xlabel("Tiempo (s)")
ax.set_ylabel("ADC Value")
ax.grid(True, alpha=0.3)

# Ejes fijos
ax.set_xlim(0, WINDOW_SECONDS)

# Estilos
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

    # Auto escala suave del eje Y
    if len(curr_y) > 0:
        valid = curr_y[curr_y > 100]  # Ignoramos ceros
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