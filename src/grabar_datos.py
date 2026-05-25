import matplotlib.pyplot as plt

def draw_neural_net(ax, left, right, bottom, top, layer_sizes):
    v_spacing = (top - bottom) / float(max(layer_sizes))
    h_spacing = (right - left) / float(len(layer_sizes) - 1)
    
    # Nodos
    for n, layer_size in enumerate(layer_sizes):
        layer_top = v_spacing*(layer_size - 1)/2. + (top + bottom)/2.
        for m in range(layer_size):
            circle = plt.Circle((n*h_spacing + left, layer_top - m*v_spacing), v_spacing/3.,
                                color='skyblue', ec='black', zorder=4)
            ax.add_artist(circle)
            
    # Conexiones (Sinapsis)
    for n, (layer_size_a, layer_size_b) in enumerate(zip(layer_sizes[:-1], layer_sizes[1:])):
        layer_top_a = v_spacing*(layer_size_a - 1)/2. + (top + bottom)/2.
        layer_top_b = v_spacing*(layer_size_b - 1)/2. + (top + bottom)/2.
        for m in range(layer_size_a):
            for o in range(layer_size_b):
                line = plt.Line2D([n*h_spacing + left, (n + 1)*h_spacing + left],
                                  [layer_top_a - m*v_spacing, layer_top_b - o*v_spacing], c='gray', alpha=0.5)
                ax.add_artist(line)

# Configuración del lienzo
fig = plt.figure(figsize=(12, 8))
ax = fig.gca()
ax.axis('off')

# Arquitectura estricta: 6 -> 16 -> 8 -> 1
arquitectura = [6, 16, 8, 1]

# Dibujar la red
draw_neural_net(ax, .1, .9, .1, .9, arquitectura)

# Añadir textos explicativos
plt.text(0.1, 0.95, 'Capa de Entrada\n(6 Inputs - Hermite)', ha='center', fontsize=12, fontweight='bold')
plt.text(0.36, 0.95, '1ª Capa Oculta\n(16 Neuronas - ReLU)', ha='center', fontsize=12, fontweight='bold')
plt.text(0.63, 0.95, '2ª Capa Oculta\n(8 Neuronas - ReLU)', ha='center', fontsize=12, fontweight='bold')
plt.text(0.9, 0.95, 'Capa de Salida\n(1 Neurona - Sigmoide)', ha='center', fontsize=12, fontweight='bold')

# Guardar la imagen
plt.savefig("red_neuronal_tfg.png", dpi=300, bbox_inches='tight')
print("Imagen generada y guardada como 'red_neuronal_tfg.png'")
plt.show()