import pandas as pd
import matplotlib.pyplot as plt

print("Cargando datos grabados...")

try:
    # 1. Leer los archivos CSV
    df_senal = pd.read_csv('ecg_signal_suj1.csv')
    df_latidos = pd.read_csv('ecg_latidos_suj1.csv')

    # 2. Configurar la gráfica
    plt.figure(figsize=(15, 6))
    plt.title("Reproducción de Grabación ECG (Datos Reales de CSV)")
    plt.xlabel("Tiempo total (Segundos)")
    plt.ylabel("Valor ADC")
    plt.grid(True, alpha=0.4)

    # 3. Dibujar la línea azul continua (Toda la señal)
    plt.plot(df_senal['Tiempo(s)'], df_senal['Valor_ADC'], color='#007acc', lw=1.0, label='Señal ECG Grabada')

    # 4. Separar por clases y pintar los puntos
    if not df_latidos.empty:
        # Filtramos los datos
        sanos = df_latidos[df_latidos['Clase'] == 0]
        arritmias = df_latidos[df_latidos['Clase'] != 0]

        # Dibujar los puntos VERDES (Latidos sanos)
        if not sanos.empty:
            plt.plot(sanos['Tiempo(s)'], sanos['Amplitud'], 'go', markersize=8, zorder=5, label='Latido Sano (Clase 0)')

        # Dibujar los puntos ROJOS (Arritmias)
        if not arritmias.empty:
            plt.plot(arritmias['Tiempo(s)'], arritmias['Amplitud'], 'ro', markersize=8, zorder=5,
                     label='Arritmia (Clase = 1)')

    # 5. Mostrar la gráfica interactiva (con zoom)
    plt.legend(loc='upper right')
    plt.tight_layout()
    print("Mostrando gráfica. Puedes hacer zoom. Cierra la ventana para terminar.")
    plt.show()

except FileNotFoundError:
    print("[X] ERROR: No se han encontrado los archivos CSV. Ejecuta primero el grabador.")