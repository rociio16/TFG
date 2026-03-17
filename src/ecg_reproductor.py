import pandas as pd
import matplotlib.pyplot as plt

print("Cargando datos grabados...")

try:
    # 1. Leer los archivos CSV
    df_senal = pd.read_csv('ecg_senal_cruda.csv')
    df_latidos = pd.read_csv('ecg_latidos_detectados.csv')

    # 2. Configurar la gráfica
    plt.figure(figsize=(15, 6))
    plt.title("Reproducción de Grabación ECG")
    plt.xlabel("Tiempo total (Segundos)")
    plt.ylabel("Valor ADC")
    plt.grid(True, alpha=0.4)

    # 3. Dibujar la línea azul continua (Toda la señal)
    plt.plot(df_senal['Tiempo(s)'], df_senal['Valor_ADC'], color='#007acc', lw=1.0, label='Señal ECG Grabada')

    # 4. Dibujar los puntos de los latidos detectados
    if not df_latidos.empty:
        # Puntos verdes para los Normales (Clase 0)
        normales = df_latidos[df_latidos['Clase'] == 0]
        if not normales.empty:
            plt.scatter(normales['Tiempo(s)'], normales['Amplitud'], color='green', marker='o', s=60, zorder=5, label='Latido Normal (C:0)')

        # Puntos rojos para las Arritmias (Clase 1)
        arritmias = df_latidos[df_latidos['Clase'] == 1]
        if not arritmias.empty:
            plt.scatter(arritmias['Tiempo(s)'], arritmias['Amplitud'], color='red', marker='X', s=80, zorder=5, label='Arritmia detectada (C:1)')

    # 5. Mostrar la gráfica interactiva (Puedes hacer zoom con el ratón)
    plt.legend(loc='upper right')
    plt.tight_layout()
    print("Mostrando gráfica. Cierra la ventana para terminar.")
    plt.show()

except FileNotFoundError:
    print("[X] ERROR: No se han encontrado los archivos CSV. Ejecuta primero el grabador.")