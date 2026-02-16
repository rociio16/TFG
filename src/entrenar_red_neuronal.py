import pandas as pd
import numpy as np
from sklearn.neural_network import MLPClassifier
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix

# 1. CARGAR DATOS
print("--- CARGANDO DATASET MIT-BIH ---")
df = pd.read_csv('mit_bih_hermite.csv')

# Separar Inputs (X) y Etiquetas (y)
X = df.drop('label', axis=1).values
y = df['label'].values

# 2. PREPROCESADO (IMPORTANTE PARA REDES NEURONALES)
# Las redes funcionan mejor si los datos están cerca de 0.
# Calculamos la media y desviación para 'normalizar' los datos.
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# Guardamos estos valores para usarlos en el C
me = scaler.mean_
sc = scaler.scale_

# Separar en entrenamiento (80%) y test (20%)
X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

# 3. CREAR Y ENTRENAR LA RED NEURONAL (MLP)
# 6 entradas -> 16 neuronas ocultas -> 1 salida
print("--- ENTRENANDO RED NEURONAL (Esto puede tardar un poco...) ---")
clf = MLPClassifier(hidden_layer_sizes=(16,),
                    activation='relu',
                    solver='adam',
                    max_iter=500,
                    random_state=42)
clf.fit(X_train, y_train)

# 4. EVALUAR
print("\n--- RESULTADOS DEL MODELO ---")
y_pred = clf.predict(X_test)
print(classification_report(y_test, y_pred))
print("Matriz de Confusión:")
print(confusion_matrix(y_test, y_pred))

# 5. GENERAR CÓDIGO C (TINYML)
# Extraemos los pesos (weights) y sesgos (biases) aprendidos
w1 = clf.coefs_[0]  # Pesos capa entrada -> oculta
b1 = clf.intercepts_[0]  # Sesgo oculta
w2 = clf.coefs_[1]  # Pesos capa oculta -> salida
b2 = clf.intercepts_[1]  # Sesgo salida

print("\n" + "=" * 60)
print("   COPIA ESTE CÓDIGO EN TU main.c (Sustituye a la función vieja)")
print("=" * 60)

# Generamos el código C para normalizar
print("// --- PARÁMETROS DE NORMALIZACIÓN (StandardScaler) ---")
print(f"const float MEAN[6] = {{ {', '.join([f'{x:.4f}' for x in me])} }};")
print(f"const float SCALE[6] = {{ {', '.join([f'{x:.4f}' for x in sc])} }};")

# Generamos los arrays de pesos
print("\n// --- PESOS DE LA RED NEURONAL (MLP 6x16x1) ---")
print(f"const float W1[6][16] = {{")
for i in range(6):
    fila = ", ".join([f"{w1[i][j]:.4f}" for j in range(16)])
    print(f"  {{ {fila} }},")
print("};")

print(f"\nconst float B1[16] = {{ {', '.join([f'{x:.4f}' for x in b1])} }};")

print(f"\nconst float W2[16] = {{ {', '.join([f'{x[0]:.4f}' for x in w2])} }};")
print(f"const float B2 = {b2[0]:.4f};")

# Generamos la función de inferencia en C
print(f"""
// Función de activación ReLU (si x<0 -> 0, si no x)
float relu(float x) {{
    return (x > 0.0f) ? x : 0.0f;
}}

// CLASIFICADOR RED NEURONAL
int clasificar_latido(float *raw_coeffs) {{
    float hidden[16];
    float output = 0.0f;

    // 1. NORMALIZAR DATOS Y CAPA OCULTA
    for (int j = 0; j < 16; j++) {{
        float sum = B1[j]; // Empezamos con el bias
        for (int i = 0; i < 6; i++) {{
            // Normalizamos el dato de entrada antes de multiplicar
            float norm_input = (raw_coeffs[i] - MEAN[i]) / SCALE[i];
            sum += norm_input * W1[i][j];
        }}
        hidden[j] = relu(sum); // Aplicamos ReLU
    }}

    // 2. CAPA DE SALIDA
    float sum_out = B2;
    for (int j = 0; j < 16; j++) {{
        sum_out += hidden[j] * W2[j];
    }}

    // Función Sigmoide simplificada: si sum_out > 0 es clase 1, si no 0
    // (Matemáticamente sigmoid(0) = 0.5)
    if (sum_out > 0.0f) {{
        return 1; // RUIDO / ARRITMIA
    }} else {{
        return 0; // NORMAL
    }}
}}
""")
print("=" * 60)