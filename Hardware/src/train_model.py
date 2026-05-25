import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.tree import DecisionTreeClassifier
from sklearn.metrics import accuracy_score
import numpy as np

# 1. CARGAR DATOS (ROBUSTO)
print("--- CARGANDO DATASET ---")
filename = 'datos_ecg.csv'
columnas = ["c0", "c1", "c2", "c3", "c4", "c5", "label"]

try:
    # Leemos sin cabecera y asignamos nombres manualmente
    df = pd.read_csv(filename, header=None, names=columnas)

    # Limpieza: Si la primera fila es texto (porque ya tenía cabecera), la borramos
    if isinstance(df.iloc[0]['c0'], str):
        df = df.iloc[1:].reset_index(drop=True)

    # Convertimos todo a números
    df = df.apply(pd.to_numeric)
    print(f"Datos cargados: {len(df)} latidos.")

except FileNotFoundError:
    print("ERROR: No se encuentra 'datos_ecg.csv'")
    exit()
except Exception as e:
    print(f"Error leyendo CSV: {e}")
    exit()

# 2. ENTRENAR
X = df.drop('label', axis=1)
y = df['label']

X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

# Usamos max_depth=4 para que el código C sea cortito y legible
clf = DecisionTreeClassifier(max_depth=4, random_state=42)
clf.fit(X_train, y_train)

# Evaluar
acc = accuracy_score(y_test, clf.predict(X_test))
print(f"\n--- PRECISIÓN DEL MODELO: {acc * 100:.2f}% ---")
if acc < 0.8:
    print("(AVISO: La precisión es baja. Quizás los datos de 'ruido' y 'normal' se parecen mucho)")


# 3. GENERAR CÓDIGO C (SOLUCIÓN DEFINITIVA CON F-STRINGS)
def tree_to_c(tree, feature_names):
    tree_ = tree.tree_
    feature_name = [
        feature_names[i] if i != -2 else "undefined!"
        for i in tree_.feature
    ]

    def recurse(node, depth):
        indent = "  " * depth

        # Si NO es una hoja (es un nodo de decisión)
        if tree_.feature[node] != -2:
            name = feature_name[node]
            threshold = tree_.threshold[node]
            idx = int(name.replace("c", ""))  # c0 -> 0

            left_code = recurse(tree_.children_left[node], depth + 1)
            right_code = recurse(tree_.children_right[node], depth + 1)

            # Usamos f-strings para evitar líos con las llaves {}
            return (f"{indent}if (coeffs[{idx}] <= {threshold:.2f}) {{\n"
                    f"{left_code}"
                    f"{indent}}} else {{\n"
                    f"{right_code}"
                    f"{indent}}}")
        else:
            # Es una hoja (resultado final)
            value = tree_.value[node][0]
            clase = np.argmax(value)
            return f"{indent}return {clase}; // 0=Normal, 1=Ruido"

    return recurse(0, 1)


print("\n" + "=" * 40)
print("   COPIA ESTE CÓDIGO EN TU PROYECTO C   ")
print("=" * 40)

c_code = tree_to_c(clf, X.columns)

print("int clasificar_latido(float *coeffs) {")
print(c_code)
print("}")
print("=" * 40)