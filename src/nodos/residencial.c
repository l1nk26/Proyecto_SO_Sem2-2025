#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "funciones_auxiliares.h"
#include "ipc_utils.h"

// Constantes para el nodo residencial
#define HORAS_DIA 12
#define MAX_SOLICITUDES_RESIDENCIAL 200
#define MIN_SOLICITUDES_RESIDENCIAL 50
#define PROBABILIDAD_RESERVA 0.5
#define LIMITE_CONSUMO_CRITICO 500.0
#define MAX_USERS 15

enum operacion_realizada {NINGUNA = 0, RESERVACION, CANCELACION, CONSULTA_PRESION};
enum estado_de_finalizacion {DESCONOCIDO = 0, AMONESTADO, EXITO};

typedef struct {
    int usuario_id; // iniciado en 0
    int hilo_id; // iniciado en 0
    int dia_simulacion;
    struct timespec tiempo_espera_inicial;
    struct timespec tiempo_espera_final;
    int litros_consumidos;
    int op_realzada;
    int nodo_asignado;
    int edo_final;
} InfoHilo;



// Variables globales
static volatile sig_atomic_t proceso_terminado = 0;
static MemoriaCompartida *shm = NULL;


// Vector de solicitudes por hora
static int solicitudes[HORAS_DIA];


// Maximas solicitudes que pueden haber en un dia
static int max_solicitudes;



// ya no me están evaluando eficiencia en memoria wasaaaa
InfoHilo informacion_hilos[DIAS_SIMULACION][HORAS_DIA][MAX_SOLICITUDES_RESIDENCIAL];



// funciona para saber cuantas solicitudes son hechas en un dia en una hora.
int numero_solicitudes[DIAS_SIMULACION][HORAS_DIA] = {0};

pthread_t hilos[HORAS_DIA][MAX_SOLICITUDES_RESIDENCIAL]; // depues puede cambiarse para lograr mejor eficiencia en espacio

sem_t sem_dia;

bool dia_terminado = false;

static void inicializar_semaforos() {
    sem_init(&sem_dia, 0, 1);
}

// consultar si el dia termino
static bool dia_termino() {
    bool x;
    sem_wait(&sem_dia);
    x = dia_terminado;
    sem_post(&sem_dia);
    return x;
}

static void dia_set(bool terminado) {
    sem_wait(&sem_dia);
    dia_terminado = terminado;
    sem_post(&sem_dia);
}



// Prototipos CONFIG
static void manejador_senal(int sig);
static void inicializar_y_configurar(void);

// Funciones de simulación
static void generar_numero_solicitudes(void);
static void actualizar_nro_solicitudes(int dia_i, int hora_i);
static void procesar_solicitud(int dia_i, int hora_i);
void cerrar_solicitudes();
void cleanup_handler(void *arg);
static void* hilo_solicitud(void *arg);
static void esperar_asignacion(InfoHilo *info);
static void consumir_agua(InfoHilo *info);
static void cancelar_solicitud(InfoHilo *info);
static void generar_amonestacion(InfoHilo *info);
static void pagar_tarifa_excedente(InfoHilo *info, int nodo_id);
static void consultar_presion(InfoHilo *info);
static double generar_consumo_litros(void);
static int obtener_nodo_disponible(void);
static int tiene_reserva(InfoHilo *info);

static void crear_solicitudes() {
    // asignar el maximo de solicitudes para el dia
    max_solicitudes = rand() % (MAX_SOLICITUDES_RESIDENCIAL - MIN_SOLICITUDES_RESIDENCIAL + 1) + MIN_SOLICITUDES_RESIDENCIAL;
    set_max_solicitudes_residencial(shm, max_solicitudes);
    
    // Generar numero de solicitudes para el dia
    generar_numero_solicitudes();
}

int main(void) {
    inicializar_y_configurar();

    // Conectar a memoria compartida
    shm = conectar_shm();
    if (shm == NULL) {
        fprintf(stderr, "[Residencial] Error: No se pudo conectar a memoria compartida\n");
        return EXIT_FAILURE;
    }
    
    printf("[Residencial] (%06ld) Proceso iniciado (PID: %d)\n", obtener_timestamp_ms(), getpid());
    
    inicializar_semaforos();

    // Bucle principal de simulación
    while (shm->simulacion_activa && !proceso_terminado) {

        crear_solicitudes();

        sem_post(&shm->sem_residencial_escoge_maximo); // para dar el permiso a residencial para que calcule su maximo de solicitudes

        // Inicializar la variable global de hilos
        memset(hilos, 0, sizeof(hilos));

        sem_post(&shm->sem_residencial_listo);

        dia_set(false);

        // 12 horas
        for (int i = 0; i < HORAS_DIA; i++) {

            int hora_actual = i + HORA_INICIO;
            int dia_actual = shm->dia_actual;
            
            if (!shm->simulacion_activa || proceso_terminado) break;
            
            // actualizar indice de informacion de hilos para acceder a informacion_hilos
            actualizar_nro_solicitudes(dia_actual - 1, i);
            
            // esperar señal de hora simulada
            sem_wait(&shm->sem_nodo_residencial);
            
            printf("[Residencial] (%06ld) Día %d, Hora %d: Generando solicitudes...\n", 
            obtener_timestamp_ms(), dia_actual, hora_actual);

            // generar hilos
            procesar_solicitud(dia_actual - 1, i);

            sem_wait(&shm->sem_nodo_residencial);

            for (int nodo = 0; nodo < NUM_NODOS; nodo++){
                liberar_nodo(shm, nodo);
            }

        }
        
        sem_wait(&shm->sem_nodo_dia_fin);

        dia_set(true);

        cerrar_solicitudes();

        sem_post(&shm->sem_residencial_listo);
    }
    
    // Limpieza
    desconectar_shm(shm);
    printf("[Residencial] (%06ld) Proceso terminado\n", obtener_timestamp_ms());
    
    return EXIT_SUCCESS;
}

// CONFIG
static void inicializar_y_configurar(void) {
    // Configurar manejador de señales
    struct sigaction sa;
    sa.sa_handler = manejador_senal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    // Inicializar semilla aleatoria
    srand(time(NULL) ^ getpid());


}   

// CONFIG
static void manejador_senal(int sig) {
    (void)sig;
    proceso_terminado = 1;
    printf("[Residencial] (%06ld) Señal recibida. Terminando proceso...\n", obtener_timestamp_ms());
}

// [MIN_SOLICITUDES to MAX_SOLICITUDES] dependiendo de max_solicitudes
static void generar_numero_solicitudes(void) {

    int total_solicitudes = 0;
    
    for (int i = 0; i < HORAS_DIA; i++) {
        solicitudes[i] = poisson_ppf(max_solicitudes / (double)HORAS_DIA, (double)rand() / RAND_MAX); // solicitudes por hora
        if (total_solicitudes >= max_solicitudes) {
            solicitudes[i] = 0;
            continue;
        }
        else {
            if (total_solicitudes + solicitudes[i] <= max_solicitudes) {
                total_solicitudes += solicitudes[i];
            }
            else{
                solicitudes[i] = max_solicitudes - total_solicitudes;
                total_solicitudes += solicitudes[i];
            }
        }
    }
    // Debug: imprimir solicitudes generadas
    /* printf("[Residencial] Solicitudes generadas: ");
    for (int i = 0; i < HORAS_DIA; i++) {
        printf("%d ", solicitudes[i]);
    } */
    //printf("maximo: %d total: %d\n", max_solicitudes, total_solicitudes);
}

// dia_i y hora_i van 0-X
static void actualizar_nro_solicitudes(int dia_i, int hora_i) {
    numero_solicitudes[dia_i][hora_i] = solicitudes[hora_i];
}

// Procesar solicitudes de esta hora y dia (recibe indice de dia, hora y la tabla de hilos)
static void procesar_solicitud(int dia_i, int hora_i) {
    // Crear hilo para cada solicitud

    for (int i = 0; i < solicitudes[hora_i]; i++) {
        
        // Inicializamos su espacio
        InfoHilo *info = &informacion_hilos[dia_i][hora_i][i];
        info->usuario_id = rand() % MAX_USERS;
        info->hilo_id = i;
        // info->tiempo_espera = 0; // tiempo de espera en ms - no existe en la estructura
        info->litros_consumidos = 0;
        info->op_realzada = NINGUNA;
        info->edo_final = DESCONOCIDO;
        
        
        pthread_create(&hilos[hora_i][i], NULL, hilo_solicitud, &informacion_hilos[dia_i][hora_i][i]);
    }

}

// Finalizar los hilos
void cerrar_solicitudes() {

    for (int nodo = 0; nodo < NUM_NODOS; nodo++) {
        liberar_nodo(shm, nodo);
    }

    for (int hora = 0; hora < HORAS_DIA; hora++) {
        for (int i = 0; i < solicitudes[hora]; i++) {
            pthread_join(hilos[hora][i], NULL);
        }
    }
}

// Limpieza antes de matar los hilos
void cleanup_handler(void *arg) {
    InfoHilo *datos = (InfoHilo *)arg;
    datos->edo_final = DESCONOCIDO;
}

// Función que ejecutarán los hilos
static void* hilo_solicitud(void *arg) {
    InfoHilo *info = (InfoHilo *)arg;
    unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL); 
    pthread_cleanup_push(cleanup_handler, info);

    clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);

    // Generar número aleatorio para determinar la acción
    double prob = (double)rand_r(&seed) / RAND_MAX;

    double s = 0;
    if (prob < PROBABILIDAD_RESERVA) {
        s = rand_r(&seed) / (double)RAND_MAX * 0.5; // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
        usleep(1000000 * s);
        pthread_testcancel();
        printf("[Residencial] (%06ld) Solicitud de reserva, usuario %d\n", obtener_timestamp_ms(), info->usuario_id);

        esperar_asignacion(info);
    } else if (prob < 0.75) {
        s = rand_r(&seed) / (double)RAND_MAX; // en cualquier momento se puede consultar presion
        usleep(1000000 * s);
        pthread_testcancel();
        printf("[Residencial] (%06ld) Solicitud de consulta de presión, usuario %d\n", obtener_timestamp_ms(), info->usuario_id);

        consultar_presion(info);
    } else { // en los primeros 45 minutos se cancelan solicitudes
        s = rand_r(&seed) / (double)RAND_MAX * 0.75;
        usleep(1000000 * s);
        pthread_testcancel();   
        printf("[Residencial] (%06ld) Solicitud de cancelación, usuario %d\n", obtener_timestamp_ms(), info->usuario_id);

        cancelar_solicitud(info);
    }

    // aumentar el numero de consultas
    lock_metricas(shm);
    shm->total_consultas_realizadas++;
    unlock_metricas(shm);

    // Sincronizar salida para evitar mensajes mezclados
    printf("[Residencial] (%06ld) Hilo %d terminado\n", obtener_timestamp_ms(), info->usuario_id);
    fflush(stdout);

    pthread_cleanup_pop(1);
    return NULL;
}

// Estado de esperar_asignacion -> reserva -> consumo
static void esperar_asignacion(InfoHilo *info) {
    
    pthread_rwlock_wrlock(&shm->mutex_nodos);
    int nodo = obtener_nodo_disponible();
    if (nodo == -1) {
        lock_metricas(shm);
        // Esto va aqui o en la consulta??
        shm->total_nodos_encontrados_ocupados++;
        unlock_metricas(shm);
    }
    pthread_rwlock_unlock(&shm->mutex_nodos);
    
    sem_wait(&shm->sem_nodos_libres);
    
    pthread_rwlock_wrlock(&shm->mutex_nodos);

    if (dia_termino()) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final));
        double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
        lock_metricas(shm);
        shm->tiempo_espera_total_ms += tiempo_espera;
        unlock_metricas(shm);
        return;
    }

    if (tiene_reserva(info) != -1) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        // cambiar el estado a desconocido
        info->edo_final = DESCONOCIDO;
        return;
    }

    nodo = obtener_nodo_disponible();
    if (nodo == -1) {
        pthread_rwlock_unlock(&shm->mutex_nodos);

        info->edo_final = DESCONOCIDO;
        return;
    }

    reservar_nodo(shm, nodo, info->usuario_id);

    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final));
    double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
    
    consumir_agua(info);
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);

    pthread_rwlock_unlock(&shm->mutex_nodos);
}

// Consumir
static void consumir_agua(InfoHilo *info) {
    double consumo = generar_consumo_litros();
    
    lock_metricas(shm);
    shm->total_metros_cubicos += consumo / 1000.0;
    if (consumo > LIMITE_CONSUMO_CRITICO) {
        shm->senales_criticas++;
        printf("[Residencial] (%06ld) Solicitud %d: Consumo crítico de %.2f litros\n", 
                obtener_timestamp_ms(), info->usuario_id, consumo);
    } 
    else 
    {
        shm->senales_estandar++;
    }
    unlock_metricas(shm);
    
    printf("[Residencial] (%06ld) Solicitud %d: Consumo  %.2f litros\n", 
           obtener_timestamp_ms(), info->usuario_id, consumo);
}

// Estado de cancelar_solicitud -> generar_amonestacion | consumo
static void cancelar_solicitud(InfoHilo *info) {
    pthread_rwlock_wrlock(&shm->mutex_nodos);

    int nodo = tiene_reserva(info);
    
    if (nodo < 0) {
        generar_amonestacion(info);
        info->edo_final = AMONESTADO;
    }
    else {
        pagar_tarifa_excedente(info, nodo);
        liberar_nodo(shm, nodo);
        info->edo_final = EXITO;
    }

    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final)); // fin de la espera del hilo
    double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);

    pthread_rwlock_unlock(&shm->mutex_nodos);
}

static void generar_amonestacion(InfoHilo *info) {
    lock_metricas(shm);
    shm->amonestaciones_digitales++;
    unlock_metricas(shm);
}

static void pagar_tarifa_excedente(InfoHilo *info, int nodo_id) {
    liberar_nodo(shm, nodo_id);
}

static void consultar_presion(InfoHilo *info) {
    // Simular consulta de presión - usar rdlock global
    pthread_rwlock_rdlock(&shm->mutex_nodos);
    
    int nodos_disponibles = 0;
    for (int i = 0; i < NUM_NODOS; i++) {
        if (!shm->valvulas[i].ocupado) {
            nodos_disponibles++;
        }
    }
    
    pthread_rwlock_unlock(&shm->mutex_nodos);

    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final)); // fin de la espera del hilo
    double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);
    
    printf("[Residencial] (%06ld) Solicitud %d: Presión disponible - %d/%d nodos libres\n", 
           obtener_timestamp_ms(), info->usuario_id, nodos_disponibles, NUM_NODOS);

    
}


// Generar consumo entre 50 y 750 litros
static double generar_consumo_litros(void) {
    unsigned int seed = (unsigned int)pthread_self();

    return 50.0 + (rand_r(&seed) % 701);
}

// retorna el nodo reservado o -1
static int obtener_nodo_disponible(void) {
    for (int i = 0; i < NUM_NODOS; i++) {
        if (!shm->valvulas[i].ocupado) {
            return i;
        }
    }
    return -1; // No hay nodos disponibles
}

// retorna el nodo reservado o -1
static int tiene_reserva(InfoHilo *info) {
    for (int i = 0; i < NUM_NODOS; i++) {
        if (shm->valvulas[i].ocupado && shm->valvulas[i].usuario_id == info->usuario_id) {
            return i;
        }
    }
    return -1;
}