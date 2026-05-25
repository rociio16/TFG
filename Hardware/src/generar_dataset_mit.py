import sys
import subprocess
import os

# --- AUTO-INSTALACIÓN DE LIBRERÍAS SI FALLAN ---
try:
    import wfdb
    import pandas as pd
    import numpy as np
    import scipy
except ImportError:
    print("--- Instalando librerías faltantes... ---")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "wfdb", "pandas", "numpy", "scipy"])
    import wfdb
    import pandas as pd
    import numpy as np

import math

# --- CONFIGURACIÓN ---
FS_MIT = 360
WINDOW_SIZE = 72
HALF_WINDOW = 36
NUM_COEFFS = 6
SIGMA = 5.0

# 0 = Normal, 1 = Ventricular (Arritmia)
CLASES_ACEPTADAS = {'N': 0, 'V': 1}


def hermite_poly(t, n, sigma):
    x = t / sigma
    if n == 0:
        return 1.0
    elif n == 1:
        return 2.0 * x

    H_prev2 = 1.0
    H_prev1 = 2.0 * x
    Hn = 0.0

    for i in range(2, n + 1):
        Hn = 2.0 * x * H_prev1 - 2.0 * (i - 1) * H_prev2
        H_prev2 = H_prev1
        H_prev1 = Hn

    gauss = math.exp(-(t ** 2) / (2 * sigma ** 2))
    normalization = 1.0 / math.sqrt(sigma * (2 ** n) * math.factorial(n) * math.sqrt(math.pi))
    return normalization * gauss * Hn


def procesar_latido_hermite(segmento):
    coeffs = []
    t_axis = np.arange(-HALF_WINDOW, HALF_WINDOW)
    for n in range(NUM_COEFFS):
        suma = 0.0
        for i, t in enumerate(t_axis):
            if i < len(segmento):
                h_val = hermite_poly(t, n, SIGMA)
                suma += segmento[i] * h_val
        coeffs.append(suma)
    return coeffs


def main():
    registros = ['100', '101', '103', '106', '119', '200', '213']
    download_dir = 'mit_data_temp'

    # 1. DESCARGAR PRIMERO (Para evitar errores de argumentos en rdrecord)
    if not os.path.exists(download_dir):
        os.makedirs(download_dir)

    print(f"--- Descargando registros del MIT-BIH a '{download_dir}'... ---")
    try:
        wfdb.dl_database('mitdb', download_dir, records=registros)
        print("Descarga completada correctamente.\n")
    except Exception as e:
        print(f"Aviso descarga: {e}")
        print("Intentando leer archivos si ya existen...\n")

    datos_finales = []

    # 2. LEER DESDE EL DISCO DURO
    print("--- Procesando latidos... ---")

    for rec_name in registros:
        path_archivo = os.path.join(download_dir, rec_name)
        try:
            # Leemos LOCALMENTE (sin argumentos de red)
            # Solo pasamos la ruta del archivo sin extensión
            record = wfdb.rdrecord(path_archivo)
            annotation = wfdb.rdann(path_archivo, 'atr')

            senal = record.p_signal[:, 0]
            picos = annotation.sample
            simbolos = annotation.symbol

            latidos_ok = 0

            for i, pico in enumerate(picos):
                simbolo = simbolos[i]
                if simbolo in CLASES_ACEPTADAS:
                    clase = CLASES_ACEPTADAS[simbolo]
                    inicio = pico - HALF_WINDOW
                    fin = pico + HALF_WINDOW

                    if inicio >= 0 and fin < len(senal):
                        segmento = senal[inicio:fin]
                        coeffs = procesar_latido_hermite(segmento)
                        fila = coeffs + [clase]
                        datos_finales.append(fila)
                        latidos_ok += 1

            print(f"Registro {rec_name}: {latidos_ok} latidos extraídos.")

        except Exception as e:
            print(f"Error leyendo registro {rec_name}: {e}")
            print(f"Asegúrate de que {path_archivo}.dat y .hea existen.")

    # 3. GUARDAR CSV
    if len(datos_finales) > 0:
        cols = [f"c{i}" for i in range(NUM_COEFFS)] + ["label"]
        df = pd.DataFrame(datos_finales, columns=cols)
        filename = "mit_bih_hermite.csv"
        df.to_csv(filename, index=False)
        print("\n" + "=" * 40)
        print(f"¡ÉXITO TOTAL! CSV GENERADO: {filename}")
        print(f"Latidos totales: {len(df)}")
        print(df['label'].value_counts())
        print("=" * 40)
    else:
        print("\nERROR CRÍTICO: No se generaron datos.")


if __name__ == "__main__":
    main()