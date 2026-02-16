#No probado aún!!! Este script recoge gran cantidad de datos del
# MIT y genera un csv para poder emplearlo para entrenar al modelo.
# Se hace así porque el MIT no ofrede CSV directamente.

# Previa instalación:  pip install wfdb numpy pandas scipy

import wfdb
import numpy as np
import pandas as pd
import math
import os

# CONFIGURACIÓN (Debe coincidir con tu código en C)
FS_MIT = 360  # La base de datos MIT ya está a 360Hz
WINDOW_SIZE = 72  # Ventana total
HALF_WINDOW = 36  # Mitad
NUM_COEFFS = 6  # Coeficientes Hermite
SIGMA = 5.0  # Sigma usado en C

# Mapeo de anotaciones del MIT a nuestras clases
# N = Normal, V = Contracción Ventricular Prematura (Arritmia común)
# Ignoramos el resto para simplificar por ahora
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
    # Generamos el eje de tiempo centrado igual que en C (-36 a +36)
    t_axis = np.arange(-HALF_WINDOW, HALF_WINDOW)

    for n in range(NUM_COEFFS):
        suma = 0.0
        # Producto punto (proyección)
        for i, t in enumerate(t_axis):
            if i < len(segmento):
                h_val = hermite_poly(t, n, SIGMA)
                suma += segmento[i] * h_val
        coeffs.append(suma)
    return coeffs


def descargar_y_procesar(records):
    datos_finales = []

    print(f"--- Descargando y procesando registros: {records} ---")

    for rec_name in records:
        try:
            # Descargar y leer señal y anotaciones
            print(f"Procesando registro {rec_name}...")
            record = wfdb.rdrecord(rec_name, pb_dir='mitdb')
            annotation = wfdb.rdann(rec_name, 'atr', pb_dir='mitdb')

            senal = record.p_signal[:, 0]  # Usamos solo la derivación 0 (MLII generalmente)
            picos = annotation.sample
            simbolos = annotation.symbol

            for i, pico in enumerate(picos):
                simbolo = simbolos[i]

                if simbolo in CLASES_ACEPTADAS:
                    clase = CLASES_ACEPTADAS[simbolo]

                    # Extraer ventana
                    inicio = pico - HALF_WINDOW
                    fin = pico + HALF_WINDOW

                    if inicio >= 0 and fin < len(senal):
                        segmento = senal[inicio:fin]

                        # Normalizar señal (importante para que la amplitud no afecte tanto)
                        # En la placa a veces no se normaliza por velocidad,
                        # pero para entrenar la red ayuda mucho.
                        # Aquí usaremos la señal cruda escalada similar al ADC si quisiéramos
                        # Pero por ahora usamos float directo.

                        # CALCULAR HERMITE
                        coeffs = procesar_latido_hermite(segmento)

                        # Guardar
                        fila = coeffs + [clase]  # [c0, c1, ..., c5, label]
                        datos_finales.append(fila)

        except Exception as e:
            print(f"Error procesando {rec_name}: {e}")

    # CREAR CSV
    cols = [f"c{i}" for i in range(NUM_COEFFS)] + ["label"]
    df = pd.DataFrame(datos_finales, columns=cols)

    filename = "mit_bih_hermite.csv"
    df.to_csv(filename, index=False)
    print(f"\n¡ÉXITO! Dataset creado: {filename}")
    print(f"Total latidos: {len(df)}")
    print("Distribución de clases:")
    print(df['label'].value_counts())


# Usamos varios registros para tener variedad
# 100, 101, 103: Mayoría normales
# 106, 119, 213: Tienen contracciones ventriculares (PVCs - Clase 1)
registros_a_usar = ['100', '101', '103', '106', '119', '200', '213']

if __name__ == "__main__":
    descargar_y_procesar(registros_a_usar)