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
#define THRESHOLD 150 // Umbral ajustado
#define K 4

// NUEVO ---------- Buffers Hermite ----------
// Usamos float para procesar math luego, aunque el ADC sea int
float buf_latido[TAM_BUF] = {0}; 

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

// ==========================================
// FUNCIÓN DE INTELIGENCIA ARTIFICIAL (TINYML)
// ==========================================
int clasificar_latido(float *coeffs) {
    // Nodo 0: El coeficiente 1 es el discriminador principal
    if (coeffs[1] <= 1601.50) {
        return 1; // RUIDO / ANOMALÍA
    } else {
        // Rama compleja para casos dudosos
        if (coeffs[5] <= 215.50) {
            if (coeffs[0] <= 9096.00) {
                if (coeffs[4] <= 3929.50) {
                    return 1; // RUIDO
                } else {
                    return 0; // NORMAL
                }
            } else {
                return 1; // RUIDO
            }
        } else {
            if (coeffs[5] <= 258.00) {
                if (coeffs[5] <= 226.50) {
                    return 1; // RUIDO
                } else {
                    return 0; // NORMAL
                }
            } else {
                return 1; // RUIDO
            }
        }
    }
}

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
void procesar_latido(float *segmento_senal, float *coefs_out) {
    for (int n = 0; n < NUM_COEFFS; n++) {
        double suma = 0.0;
        // Recorremos la ventana. t=0 es el centro (index WINDOW)
        // El segmento tiene tamaño WINDOW*2 (aprox 72)
        for (int i = 0; i < WINDOW * 2; i++) {
            // t va desde -36 hasta +36 aprox
            double t = (double)(i - WINDOW); 
            // Proyección: Señal * FunciónBase
            suma += segmento_senal[i] * hermite(t, n, SIGMA);
        }
        coefs_out[n] = (float)suma;
    }
}

/* ========================= ALGORITMO QRS ========================= */

int algoritmo(int16_t dato){
    int latido_detectado = 0;

    // --- NUEVO: ACTUALIZAR BUFFER LARGO (BUF_LATIDO) ---
    // Desplazamos para hacer hueco (memoria del pasado)
    for (int j = 0; j < TAM_BUF - 1; j++) {
        buf_latido[j] = buf_latido[j+1];
    }
    buf_latido[TAM_BUF - 1] = (float)dato; // Guardamos dato actual al final


    //0. ACTUALIZAR SIGNAL (DERIVADA)
    for (int j = 0; j < K; j++){
        signal[j] = signal[j+1];
    }
    signal[K] = dato; 

    //1. DERIVADA
    int d = signal[K] - signal[0]; 
    int v = abs(d); 
    int idx = global_i - 4; // Ajuste de fase por derivada

    //2. MAQUINA DE ESTADOS
    switch(state){
        case RESET:
            cand_val = -1.0;
            state = LOOKING;
            break;

        case LOOKING:
            if (v > THRESHOLD){
                cand_val = v;
                cand_idx = idx;
                state = PROV;
            }
            break;

        case PROV:
            if (v > cand_val){
                cand_val = v;
                cand_idx = idx;
            }
            //Si baja a menos de la mitad del pico provisional
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
                // Si se acaba el tiempo de espera, confirmamos el latido
                if(global_i >= wait_until){
                    latido_detectado = 1;

                    // ==========================================================
                    // === ZONA DE INTEGRACIÓN HERMITE (SOLICITADO) ===
                    // ==========================================================
                    
                    // Calcular retardo: cuánto hace que pasó el pico real respecto a ahora
                    // 'global_i' es el contador actual (final del buffer)
                    // 'prov_best_idx' es donde detectamos el pico
                    int lag = global_i - prov_best_idx;
                    
                    // En nuestro buffer deslizante 'buf_latido', el dato actual está en [TAM_BUF-1].
                    // Por tanto, el pico está 'lag' posiciones atrás.
                    int idx_pico_en_buf = (TAM_BUF - 1) - lag;

                    // Definir ventana centrada en el pico
                    int inicio = idx_pico_en_buf - WINDOW;
                    int fin = idx_pico_en_buf + WINDOW;

                    // Protección para no leer fuera de memoria
                    if (inicio >= 0 && fin < TAM_BUF) {
                        float coeficientes[NUM_COEFFS];

                        // 1. Procesamos el trozo de memoria correspondiente (matemáticas)
                        procesar_latido(&buf_latido[inicio], coeficientes);

                        // 2. INTELIGENCIA ARTIFICIAL: Clasificamos
                        int clase = clasificar_latido(coeficientes);

                        // 3. ACTUAMOS: Encender o Apagar LED según la clase
                        if (clase == 1) {
                            // RUIDO DETECTADO -> ENCENDER LED
                            gpio_pin_set_dt(&led, 1); 
                            printk(">>> RUIDO (Clase 1) - LED ON\n");
                        } else {
                            // LATIDO NORMAL -> APAGAR LED
                            gpio_pin_set_dt(&led, 0);
                            printk(">>> NORMAL (Clase 0) - LED OFF\n");
                        }

                        // --- SALIDA SERIAL ---
                        // Formato: "COEFFS,c0,c1,c2,c3,c4,c5"
                        // Usamos printk para enviar por USB
                        printk("COEFFS");
                        for(int k=0; k<NUM_COEFFS; k++){
                            // Imprimimos como entero escalado x100 o float si zephyr config lo permite
                            // Para seguridad en microcontroladores simples, a veces %f da problemas.
                            // Si errores, cambiar a: (int)(coeficientes[k]*1000)
                            printk(",%d", (int)(coeficientes[k])); 
                        }
                        printk("\n");
                    }
                    // ==========================================================
                    
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
    int lo_plus = gpio_pin_get(gpio_dev, LO_PLUS_PIN);
    int lo_minus = gpio_pin_get(gpio_dev, LO_MINUS_PIN);

    ret = adc_read(adc_dev, &sequence);
    if (ret == 0) {
        // FILTRO DE RUIDO Y CABLE SUELTO
        if ((int)sample_buffer > 50) {
            int beat = algoritmo(sample_buffer);
            
            // Opcional: Imprimir señal cruda para dibujar en Python a la vez
            // Si imprimimos COEFFS arriba, aquí imprimimos la señal normal
            if (!beat){
               // Solo imprimimos señal si NO hay beat (para no solapar textos)
               // Ojo: Esto es para debug gráfico.
               printk("%d,0\n", (int)sample_buffer); 
            }
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