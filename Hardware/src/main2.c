#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
/*SOFIA*/
#include <stdlib.h> // Necesario para abs()

/* ========================= CONFIGURACIÓN DEL ADC ========================= */

#define ADC_NODE DT_NODELABEL(adc)  // Obtiene el nodo del árbol de dispositivos (Device Tree) que representa el ADC

#define ADC_CHANNEL 2  // AIN2 -> P0.04 ; Selecciona el canal analógico AIN2 del ADC (corresponde al pin P0.04 en la nRF52840)
#define ADC_RESOLUTION 12  // Resolución del ADC: 12 bits → valores de 0 a 4095
#define ADC_GAIN ADC_GAIN_1  // Ganancia aplicada a la señal de entrada (1 significa sin ganancia)
#define ADC_REFERENCE ADC_REF_INTERNAL  // Referencia de voltaje interna del ADC
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME_DEFAULT  // Tiempo de adquisición

/* Crea una estructura con la información del dispositivo ADC obtenido del Device Tree */
static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

/* Configuración del canal del ADC */
static const struct adc_channel_cfg my_channel_cfg = {
    .gain = ADC_GAIN_1_6,               // <--- CRITICO: 1/6 para leer hasta 3.6V
    .reference = ADC_REFERENCE,         // Referencia interna 0.6V
    .acquisition_time = ADC_ACQUISITION_TIME,       // Tiempo de muestreo estándar
    .channel_id = ADC_CHANNEL,                      // Canal físico del ADC a usar (AIN2)
    //.differential = 0,
#if defined(CONFIG_ADC_NRFX_SAADC)
    /* Solo si el driver NRFX SAADC está activo */
    .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput2,
#endif
};

static int16_t sample_buffer;  // Buffer donde se almacena la lectura del ADC (valor digital convertido)

/* ========================= CONFIGURACIÓN DE LOS PINES LO+ Y LO− ========================= */

/* Vamos a usar el controlador gpio0 y manejar los pines por número */
#define GPIO_PORT_NODE DT_NODELABEL(gpio0)   /* controlador GPIO0 */
static const struct device *gpio_dev = DEVICE_DT_GET(GPIO_PORT_NODE);

/* Define aquí los pines físicos que has elegido para LO+ y LO- */
#define LO_PLUS_PIN  11   /* P0.11 -> LO+ */
#define LO_MINUS_PIN 12   /* P0.12 -> LO- */

/* ========================= VARIABLES GLOBALES ========================= */

static struct adc_sequence sequence;
static struct k_timer adc_timer;
static struct k_work adc_work;   /* worker para realizar la lectura ADC de forma segura */


/*SOFIA*/
#define FS 250 // Frecuencia de muestreo en Hz => k_timer_start() cada 4ms 
//TODO mirar si hay que acmbiarla a 360Hz
#define WAIT_SAMPLES 50 // 200ms a 250 Hz (en 360 son 72)
#define THRESHOLD = 300 //TODO ajustar umbral => ADC (0-4095), 0.3 es bajo para enteros
#define k 3

//Variables de state
typedef enum { RESET, LOOKING, PROV, HALF} StateType; //TODO mirar si es la unica forma y por qué 
static StateType state = RESET; //TODO mirar por qué static

//Variables para el algoritmo
int cand_val = -1.0; // valor candidato a QRS
int cand_idx = -1; // pos del candidato a QRS
int prov_best_idx = -1; // valor def QRS
int prov_best_val = -1.0; //pos de def QRS
//TODO mirar si es mejor usar uint32_t?? no sé si int va a ser muy exacto
int wait_until = -1;

unsigned int global_i=0; //TODO por qué unsigned, en gemini pone "long" en vez de "int"
int16_t signal[K + 1] = {0}; // señal de entrada (ventana deslizante)
int last_confirmed = -10000;


int algoritmo(int16_t dato){
    int latido_detectado = 0;

    //0. ACTUALIZAR SIGNAL
    //Mover datos viejos a la izq
    for (int j = 0; j < K; j++){
        signal[j] = signal[j+1];
    }
    signal[K] = dato; //Añadir nuevo dato al final

    //1. DERIVADA
    int d = signal[K] - signal[0]; //diferencia entre el dato más reciente y el más antiguo en la ventana
    int v = abs(d); //valor absoluto de la derivada
    int idx = global_i - 4;

    //2. MAQUINA DE ESTADOS
    switch(state){
        case RESET:
            cand_val = -1.0;
            cand_idx = -1;
            prov_best_idx = -1;
            prov_best_val = -1.0;
            wait_until = -1;
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
            if (v > THRESHOLD){
                if (v > cand_val){
                    cand_val = v;
                    cand_idx = idx;
                }
            } 
            //Si baja a menos de la mirad del pico provisional
            if (v < (cand_val / 2)){
                prov_best_val = cand_val;
                prov_best_idx = cand_idx;
                wait_until = global_i + WAIT_SAMPLES;
                state = HALF;
            }
            break;

        case HALF:
            if (v > prov_best_val){
                //Nuevo candidato en el intervalo de espera
                prov_best_val = v;
                prov_best_idx = idx;
            } else {
                if(global_i >= wait_until){
                    if ((prov_best_idx - last_confirmed) > WAIT_SAMPLES){
                        latido_detectado = 1;
                        last_confirmed = prov_best_idx;
                    }
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
/* Se ejecuta en contexto de k_work, así evitamos llamar adc_read() desde el timer
   y evitamos el -EAGAIN si el ADC está ocupado. */
void adc_read_work(struct k_work *work)
{
    int ret;

    /* Leer los pines LO+ y LO− usando el controlador GPIO */
    int lo_plus = gpio_pin_get(gpio_dev, LO_PLUS_PIN);
    int lo_minus = gpio_pin_get(gpio_dev, LO_MINUS_PIN);

    /* Si hay lead-off, informamos y no hacemos la lectura ADC */
    if (lo_plus || lo_minus) {
        //printk("Lead off detected! LO+: %d, LO-: %d\n", lo_plus, lo_minus);
    }

    /* Realizar la lectura ADC (bloqueante dentro del worker, pero sin solapamientos) */
    ret = adc_read(adc_dev, &sequence);
    if (ret == 0) {
        //printk("%d\n", sample_buffer);
        /*SOFIA*/
        int beat = algoritmo(sample_buffer);
        if (beat){
            printk("Latido detectado en muestra %u (valor ADC: %d)\n", global_i, (int)sample_buffer);
        } else {
            printk("%d\n", (int)sample_buffer);
        }
    } else if (ret == -EAGAIN) {
        /* Esto raramente debería ocurrir porque el worker serializa las lecturas,
           pero lo capturamos por seguridad. */
        printk("ADC busy, will retry next cycle\n");
    } else {
        printk("ADC read error: %d\n", ret);
    }
}

/* ========================= HANDLER DEL TEMPORIZADOR ========================= */
/* Cada vez que el timer expira encolamos el work para que haga la lectura. */
void timer_handler(struct k_timer *timer_id)
{
    k_work_submit(&adc_work);
}

/* ========================= PROGRAMA PRINCIPAL ========================= */

int main(void)
{
    int ret;

    /* 1. Comprobar si el dispositivo ADC está listo para usarse */
    if (!device_is_ready(adc_dev)) {
        printk("ADC not ready\n");
        return -1;
    }

    /* 2. Aplicar la configuración definida arriba al canal seleccionado (AIN2) */
    ret = adc_channel_setup(adc_dev, &my_channel_cfg);
    if (ret < 0) {
        printk("Channel setup failed (%d)\n", ret);
        return -1;
    }

    /* 3. Comprobar si el dispositivo GPIO está listo para usarse */
    if (!device_is_ready(gpio_dev)) {
        printk("GPIO not ready\n");
        return -1;
    }

    /* 4. Configurar los pines LO+ y LO− como entradas digitales */
    gpio_pin_configure(gpio_dev, LO_PLUS_PIN, GPIO_INPUT);
    gpio_pin_configure(gpio_dev, LO_MINUS_PIN, GPIO_INPUT);

    /* 5. Configuración de la secuencia de lectura: define cómo se realizará cada lectura del ADC */
    sequence.channels = BIT(ADC_CHANNEL);          // Define qué canal se leerá (en este caso, el 2)
    sequence.buffer = &sample_buffer;              // Dónde se guarda el resultado de la conversión
    sequence.buffer_size = sizeof(sample_buffer);  // Tamaño del buffer (en bytes)
    sequence.resolution = ADC_RESOLUTION;          // Resolución de 12 bits

    /* 6. Inicializar worker y temporizador */
    k_work_init(&adc_work, adc_read_work);                 // Inicializa el worker con la función de lectura
    k_timer_init(&adc_timer, timer_handler, NULL);         // Inicializa el timer con su handler
    // <--- MEJORA: Aumentar a 4ms (250Hz) para ver la forma de onda
    k_timer_start(&adc_timer, K_MSEC(4), K_MSEC(4));

    printk("AD8232 sensor running (LO+/LO- + interrupt-driven sampling)...\n");

    /* 7. Bucle principal: la lógica se ejecuta en el worker */
    while (1) {
        k_sleep(K_FOREVER);  // Dormir hasta la próxima interrupción / evento
    }

    return 0;
}