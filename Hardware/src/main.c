#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
/*SOFIA*/
#include <stdlib.h> // Necesario para abs()
#include <math.h>   // Necesario para sqrt, exp, pow, M_PI

/* ========================= PARAMETROS HERMITES ========================= */
// NUEVO ---------- Parámetros Hermites ----------
#define SIGMA 5.0
#define NUM_COEFFS 6
#define WINDOW 36 // Mitad del latido (72/2)
#define TAM_BUF 150 // Buffer suficiente para mirar atrás
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================= CONFIGURACIÓN DEL ADC ========================= */

#define ADC_NODE DT_NODELABEL(adc)  // Obtiene el nodo del árbol de dispositivos
#define ADC_CHANNEL 2  // AIN2 -> P0.04
#define ADC_RESOLUTION 12  // 12 bits -> 0 a 4095
#define ADC_GAIN ADC_GAIN_1  // Ganancia 1
#define ADC_REFERENCE ADC_REF_INTERNAL 
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT 

static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

static const struct adc_channel_cfg my_channel_cfg = {
    .gain = ADC_GAIN_1_6,           // <--- CRITICO: 1/6 para leer hasta 3.6V
    .reference = ADC_REFERENCE,     // Referencia interna 0.6V
    .acquisition_time = ADC_ACQUISITION_TIME,
    .channel_id = ADC_CHANNEL,
#if defined(CONFIG_ADC_NRFX_SAADC)
    .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput2,
#endif
};

static int16_t sample_buffer;  // Buffer lectura ADC

/* ========================= CONFIGURACIÓN PINES ========================= */
#define GPIO_PORT_NODE DT_NODELABEL(gpio0) 
static const struct device *gpio_dev = DEVICE_DT_GET(GPIO_PORT_NODE);
#define LO_PLUS_PIN  11 
#define LO_MINUS_PIN 12 

/* ========================= CONFIGURACIÓN LED ========================= */
// Buscar el "led0" en el archivo de configuración de la placa
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* ========================= VARIABLES GLOBALES ========================= */

static struct adc_sequence sequence;
static struct k_timer adc_timer;
static struct k_work adc_work; 

/*SOFIA*/
#define FS 360 // Frecuencia de muestreo en Hz
#define WAIT_SAMPLES 72 // 200ms a 360 Hz
#define THRESHOLD 200 // Umbral ajustado
#define K 4

// NUEVO ---------- Buffers Hermite ----------
// Usamos float para procesar math luego, aunque el ADC sea int
float buf_latido[TAM_BUF] = {0}; 

// --- NUEVO: PERÍODO REFRACTARIO ---
#define REFRACTORY_SAMPLES 90 // ~250ms de tiempo ciego a 360Hz
unsigned int refractory_until = 0; // Guardará hasta qué momento estamos ciegos

//Variables de state
typedef enum { RESET, LOOKING, PROV, HALF} StateType; 
static StateType state = RESET; 

//Variables para el algoritmo
int cand_val = -1.0; 
int cand_idx = -1; 
int prov_best_idx = -1; 
int prov_best_val = -1.0; 
int wait_until = -1;

unsigned int global_i=0; 
int16_t signal[K + 1] = {0}; // señal derivada (ventana deslizante)
int last_confirmed = -10000;

// --- PARÁMETROS DE NORMALIZACIÓN (StandardScaler) ---
const float MEAN[6] = { 1.7062, -1.9870, -0.7582, -0.2196, -0.0684, -0.4849 };
const float SCALE[6] = { 1.2594, 1.7584, 0.8294, 1.1127, 0.3341, 0.6958 };

// --- PESOS CAPA 1 (6 entradas -> 16 neuronas) ---
const float W1[6][16] = {
  { -0.3058, 0.0925, 0.0142, -0.8217, -0.3890, 0.4996, 0.3925, 0.2107, -0.3388, 0.0952, -0.5865, -0.3095, 0.0239, 0.8284, 0.8381, -0.9140 },
  { 0.1643, 0.5986, -0.5198, 0.2836, -0.9794, -0.6309, 0.9403, -0.4487, -0.4012, -0.9873, -0.3837, 0.9958, -0.0797, 0.0553, 0.8691, -0.0975 },
  { -0.0910, -0.0108, -0.7484, -0.1920, 0.0285, 0.0833, 0.0851, 0.1826, 0.6885, -0.0694, -0.3043, 0.5408, 0.0856, 0.5137, -0.0764, -0.2514 },
  { -0.9381, -0.0615, 0.1191, -0.2365, 0.0948, 0.0528, 0.2338, 0.1513, -0.7063, -0.2437, -0.8848, 0.4735, 0.2291, 0.5548, 0.2533, -0.9363 },
  { -0.4655, -0.5165, 0.3867, 0.6290, 0.1565, -0.5041, -0.2502, -0.4455, 0.1198, -0.3150, -0.2462, 0.0958, 0.3849, 0.4727, -0.3867, -0.4036 },
  { -0.2563, 0.4112, 0.4559, -0.6297, 0.1295, -0.3424, -0.1769, -0.1178, 0.6395, 0.0988, -0.7302, 0.0810, -0.2783, -0.0985, -0.5805, -0.0120 },
};
const float B1[16] = { 0.6671, -0.4219, 0.0174, 0.0074, 1.0867, -0.6100, -0.1400, 0.3583, 0.3182, 0.4106, 0.5844, -0.4660, 0.2933, -0.1176, -0.0659, 0.2309 };

// --- PESOS CAPA 2 (16 neuronas -> 8 neuronas) ---
const float W2[16][8] = {
  { -0.4655, -0.4496, 0.2625, -0.6682, 0.8910, -0.5941, -0.2038, -0.5231 },
  { -0.0971, -0.1436, -0.4537, -0.4793, -0.6763, -0.9589, -0.5060, -0.5588 },
  { 0.6789, -0.2106, 0.3242, 0.3030, -0.0332, 0.4152, -0.0867, 0.5470 },
  { -0.4749, -0.0788, 0.6388, 0.1334, -0.1067, -0.3139, -0.5410, -0.2145 },
  { 0.6682, -0.4765, 0.3304, 0.7427, 1.0165, 0.2174, 0.3262, 0.6516 },
  { -0.9517, -0.0720, -0.0430, -0.5324, 0.3331, -0.0774, -0.6438, -1.1189 },
  { 0.4220, 0.0169, 0.0128, 0.3470, -2.3068, 0.2318, 0.4275, 0.5587 },
  { 0.2857, -0.2955, 0.4543, -0.1664, -0.2638, 0.2592, 0.3532, 0.5706 },
  { 0.6945, -0.0911, 0.0645, 0.6577, -0.5048, 0.7008, 0.3174, 0.5831 },
  { 0.3138, -0.0513, -0.7393, 0.8066, -0.2518, 0.3555, 0.7064, 0.5604 },
  { -0.2991, -0.4487, -0.9968, -0.4294, 0.5133, -0.5984, -0.1498, 0.0599 },
  { -0.4522, -0.4337, 0.1088, 0.1999, -1.8108, 0.0629, -0.1136, 0.1717 },
  { -0.3908, -0.1988, 0.1872, -0.3505, 0.5575, -0.0032, 0.1707, -0.5361 },
  { -0.4929, -0.0374, -1.2016, -0.2844, 0.6510, -0.6930, -0.5132, -0.5798 },
  { 0.5583, -0.2821, 0.7902, -0.0595, -0.6814, 0.0624, -0.4917, 0.3581 },
  { -0.4532, -0.4061, -0.7592, -0.9870, 0.6964, 0.0796, -0.9585, -0.7286 },
};
const float B2[8] = { 0.0264, 0.0000, -0.3300, -0.1116, 0.5584, 0.1557, 0.4034, -0.2299 };

// --- PESOS CAPA SALIDA (8 neuronas -> 1 salida) ---
const float W3[8] = { -1.2224, -0.3355, -1.3760, -1.1556, 1.7558, -0.9542, -0.9076, -0.8789 };
const float B3 = 0.9954;

// Función de activación ReLU
float relu(float x) { return (x > 0.0f) ? x : 0.0f; }

// CLASIFICADOR RED NEURONAL KERAS (2 CAPAS OCULTAS)
int clasificar_latido(float *raw_coeffs) {
    float hidden1[16];
    float hidden2[8];
    float output = 0.0f;

    // 1. NORMALIZAR DATOS Y CAPA OCULTA 1
    for (int j = 0; j < 16; j++) {
        float sum = B1[j];
        for (int i = 0; i < 6; i++) {
            float norm_input = (raw_coeffs[i] - MEAN[i]) / SCALE[i];
            sum += norm_input * W1[i][j];
        }
        hidden1[j] = relu(sum);
    }

    // 2. CAPA OCULTA 2
    for (int j = 0; j < 8; j++) {
        float sum = B2[j];
        for (int i = 0; i < 16; i++) {
            sum += hidden1[i] * W2[i][j];
        }
        hidden2[j] = relu(sum);
    }

    // 3. CAPA DE SALIDA
    float sum_out = B3;
    for (int j = 0; j < 8; j++) {
        sum_out += hidden2[j] * W3[j];
    }

    // Salida (0 = Normal, 1 = Arritmia)
    if (sum_out > 0.0f) {
        return 1; 
    } else {
        return 0; 
    }
}


#include <math.h>

// Ajusta este número según los coeficientes que uses (en tu log parecen ser 6)
#define NUM_HERMITE 6 

void normalizar_hermite(float* coeficientes, int num_coefs) {
    float max_val = 0.0f;

    // 1. Encontrar el valor absoluto máximo en el array
    for (int i = 0; i < num_coefs; i++) {
        float abs_val = fabs(coeficientes[i]);
        if (abs_val > max_val) {
            max_val = abs_val;
        }
    }

    // 2. Dividir todos los coeficientes por ese máximo (para evitar dividir por cero)
    if (max_val > 0.0f) {
        for (int i = 0; i < num_coefs; i++) {
            coeficientes[i] = coeficientes[i] / max_val;
        }
    }
}

/* ============================================================ */

/* ========================= FUNCIONES MATEMÁTICAS (HERMITE) ========================= */

// Función auxiliar para factorial
double factorial(int n) {
    double res = 1.0;
    if (n == 2) res = 2.0;
    else if (n == 3) res = 6.0;
    else if (n == 4) res = 24.0;
    else if (n == 5) res = 120.0;
    else if (n == 6) res = 720.0;
    else if (n == 7) res = 5040.0;
    return res;
}

double hermite(double t, int n, double sigma) {
    double x = t / sigma;
    double Hn = 0.0; 

    // --- 1. CALCULO DEL POLINOMIO Hn(x) ---
    if (n == 0) Hn = 1.0;
    else if (n == 1) Hn = 2.0 * x;
    else {
        double h_n_2 = 1.0;       // H0
        double h_n_1 = 2.0 * x;   // H1
        Hn = 0.0;
        for (int i = 2; i <= n; i++) {
            Hn = 2.0 * x * h_n_1 - 2.0 * (double)(i - 1) * h_n_2;
            h_n_2 = h_n_1;
            h_n_1 = Hn;
        }
    }

    // --- 2. Gaussiana ---
    double gauss = exp(- (t * t) / (2.0 * sigma * sigma));

    // --- 3. Constante normalizacion ---
    double K_norm = 1.0 / sqrt(sigma * pow(2.0, n) * factorial(n) * sqrt(M_PI));

    return K_norm * gauss * Hn;
}

// Función para proyectar el latido sobre los polinomios
// Esta función toma un trozo de señal (72 muestras) y saca 6 coeficientes
// Función para proyectar el latido sobre los polinomios
void procesar_latido(float *segmento_senal, float *coefs_out) {
    // --- NUEVO: Calcular la media para quitar el DC offset (centrar en 0) ---
    float media = 0.0;
    for (int i = 0; i < WINDOW * 2; i++) {
        media += segmento_senal[i];
    }
    media /= (WINDOW * 2);

    // Calcular Hermite
    for (int n = 0; n < NUM_COEFFS; n++) {
        double suma = 0.0;
        for (int i = 0; i < WINDOW * 2; i++) {
            double t = (double)(i - WINDOW); 
            // Restamos la media antes de multiplicar la función de Hermite
            suma += (segmento_senal[i] - media) * hermite(t, n, SIGMA);
        }
        coefs_out[n] = (float)suma;
    }
}


/* ========================= FILTRO NOTCH 50Hz ========================= */
// Memorias del filtro (valores pasados)
static float x_1 = 0.0f, x_2 = 0.0f;
static float y_1 = 0.0f, y_2 = 0.0f;

float filtrar_50hz(float x) {
    // Coeficientes exactos para Fs=360Hz y Notch en 50Hz
    float b0 = 0.99014f, b1 = -1.27289f, b2 = 0.99014f;
    float a1 = 1.27272f, a2 = -0.98010f; 

    // Aplicar la ecuación en diferencias del filtro
    float y = (b0 * x) + (b1 * x_1) + (b2 * x_2) + (a1 * y_1) + (a2 * y_2);

    // Actualizar las memorias para la siguiente lectura
    x_2 = x_1;
    x_1 = x;
    y_2 = y_1;
    y_1 = y;

    return y;
}


/* ========================= ALGORITMO QRS ========================= */
int algoritmo(int16_t dato){
    int latido_detectado = 0;
    
    // --- NUEVO: Variables estáticas para el umbral dinámico ---
    static float umbral_dinamico = 200.0f; 
    const float UMBRAL_MIN = 60.0f; // Límite para no buscar en el ruido de fondo

    // --- NUEVO: Aplicar el decay multiplicando por 0.9999 en cada muestra ---
    umbral_dinamico *= 0.9999f;
    if (umbral_dinamico < UMBRAL_MIN) {
        umbral_dinamico = UMBRAL_MIN;
    }

    // Actualizar buffer largo (buf_latido)
    for (int j = 0; j < TAM_BUF - 1; j++) {
        buf_latido[j] = buf_latido[j+1];
    }
    buf_latido[TAM_BUF - 1] = (float)dato;

    // 1. ACTUALIZAR SIGNAL (DERIVADA)
    for (int j = 0; j < K; j++){
        signal[j] = signal[j+1];
    }
    signal[K] = dato; 

    // 2. DERIVADA
    int d = signal[K] - signal[0]; 
    int v = abs(d); 
    int idx = global_i - 4; 

    // 3. MAQUINA DE ESTADOS
    switch(state){
        case RESET:
            cand_val = -1.0;
            state = LOOKING;
            break;

        case LOOKING:
            if (global_i > refractory_until) {
                // --- NUEVO: Usamos umbral_dinamico en lugar del THRESHOLD fijo ---
                if (v > umbral_dinamico){
                    cand_val = v;
                    cand_idx = idx;
                    state = PROV;
                }
            }
            break;

        case PROV:
            if (v > cand_val){
                cand_val = v;
                cand_idx = idx;
            }
            if (v < (cand_val / 2)){
                prov_best_val = cand_val;
                prov_best_idx = cand_idx;
                wait_until = prov_best_idx + WAIT_SAMPLES;
                state = HALF;
            }
            break;

        case HALF:
            if (v > prov_best_val){
                prov_best_val = v;
                prov_best_idx = idx;
                cand_val = v;
                state = PROV;
            } else {
                if(global_i >= wait_until){
                    latido_detectado = 1;

                    // --- NUEVO: Resetear el umbral al 60% del pico detectado ---
                    // Esto evita detectar la onda T como si fuera un nuevo latido
                    umbral_dinamico = prov_best_val * 0.6f;

                    // === ZONA DE INTEGRACIÓN HERMITE ===
                    int lag = global_i - prov_best_idx;
                    int idx_pico_en_buf = (TAM_BUF - 1) - lag;
                    int inicio = idx_pico_en_buf - WINDOW;
                    int fin = idx_pico_en_buf + WINDOW;

                    if (inicio >= 0 && fin < TAM_BUF) {
                        float coeficientes[NUM_COEFFS];
                        procesar_latido(&buf_latido[inicio], coeficientes);
                        int clase = clasificar_latido(coeficientes);

                        if (clase == 1) {
                            gpio_pin_set_dt(&led, 1); 
                            printk(">>> RUIDO (Clase 1) - LED ON\n");
                        } else {
                            gpio_pin_set_dt(&led, 0);
                            printk(">>> NORMAL (Clase 0) - LED OFF\n");
                        }

                        printk("COEFFS");
                        for(int k=0; k<NUM_COEFFS; k++){
                            printk(",%d", (int)(coeficientes[k] * 1000.0f));
                        }
                        printk(",CLASE:%d\n", clase);
                    }
                    
                    refractory_until = global_i + REFRACTORY_SAMPLES;
                    state = RESET;
                }
            }
            break;

        default:
            state = RESET;
            break;
    }
    global_i += 1;
    return latido_detectado;
}

/* ========================= WORKER: LECTURA ADC ========================= */
void adc_read_work(struct k_work *work)
{
    int ret;
    static int16_t last_sample = 0; // Para calcular la diferencia
    
    ret = adc_read(adc_dev, &sequence);
    if (ret == 0) {
        
        // --- 1. DETECTOR DE MOVIMIENTO BRUSCO (El "Pánico") ---
        int diff = abs((int)sample_buffer - (int)last_sample);
        last_sample = sample_buffer;

        // Si la señal cambia de golpe (ruido/movimiento), encendemos LED YA
        if (diff > 300) { 
            gpio_pin_set_dt(&led, 1);  
        }

        // --- 2. PROCESAMIENTO NORMAL DE LATIDOS ---
        if ((int)sample_buffer > 50) {
            // --- NUEVO: Aplicar el filtro de 50Hz al dato crudo ---
            float dato_limpio_float = filtrar_50hz((float)sample_buffer);
            int16_t dato_limpio = (int16_t)dato_limpio_float;

            // YA NO Imprimimos la señal cruda para que la gráfica no se rompa
            // Ahora se imprime el dato limpio
            printk("%d\n", (int)dato_limpio); 
            
            // Pasamos el latido limpio al algoritmo
            algoritmo(dato_limpio);
        } else {
            // Si sueltan el sensor, apagamos LED
            gpio_pin_set_dt(&led, 0);
        }
    } 
}

/* ========================= HANDLER DEL TEMPORIZADOR ========================= */
void timer_handler(struct k_timer *timer_id)
{
    k_work_submit(&adc_work);
}

/* ========================= PROGRAMA PRINCIPAL ========================= */
int main(void)
{
    int ret;

    if (!device_is_ready(adc_dev)) return -1;

    ret = adc_channel_setup(adc_dev, &my_channel_cfg);
    if (ret < 0) return -1;

    if (!device_is_ready(gpio_dev)) return -1;

    gpio_pin_configure(gpio_dev, LO_PLUS_PIN, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, LO_MINUS_PIN, GPIO_INPUT);

    // INICIALIZAR LED --> detectar si quieto o movimiento
    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return 0;
    }
    gpio_pin_set_dt(&led, 0); // Empezar con el LED apagado

    sequence.channels = BIT(ADC_CHANNEL);
    sequence.buffer = &sample_buffer;
    sequence.buffer_size = sizeof(sample_buffer);
    sequence.resolution = ADC_RESOLUTION;

    k_work_init(&adc_work, adc_read_work);
    k_timer_init(&adc_timer, timer_handler, NULL);
    
    // MEJORA: 2777us = 360Hz exactos
    k_timer_start(&adc_timer, K_USEC(2777), K_USEC(2777));

    printk("ECG + HERMITE SYSTEM STARTED...\n");

    while (1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}