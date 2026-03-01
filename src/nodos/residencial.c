#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <errno.h>
#include <math.h>
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


// Prototipos CONFIG
static void manejador_senal(int sig);
static void inicializar_y_configurar(void);

// Funciones de simulación
static void generar_numero_solicitudes(void)
static void actualizar_nro_solicitudes(int dia_i, int hora_i);
static void procesar_solicitud(int dia_i, int hora_i, pthread_t **hilos)
void cerrar_solicitudes_dia_actual(pthread_t **hilos);
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

int main(void) {
    inicializar_y_configurar();
    
    // Conectar a memoria compartida
    shm = conectar_shm();
    if (shm == NULL) {
        fprintf(stderr, "[Residencial] Error: No se pudo conectar a memoria compartida\n");
        return EXIT_FAILURE;
    }
    
    printf("[Residencial] Proceso iniciado (PID: %d)\n", getpid());
    
    // Bucle principal de simulación
    while (shm->simulacion_activa && !proceso_terminado) {


        // asignar el maximo de solicitudes para el dia
        max_solicitudes = rand() % (MAX_SOLICITUDES_RESIDENCIAL - MIN_SOLICITUDES_RESIDENCIAL + 1) + MIN_SOLICITUDES_RESIDENCIAL;
        shm->set_max_solicitudes_residencial(max_solicitudes);

        
        sem_post(shm->sem_nodo_industrial); // para dar el permiso a industrial para que calcule su maximo de solicitudes
        
        // Generar numero de solicitudes para el dia
        generar_numero_solicitudes();
        
        // en ecoflow se debe hacer un wait antes de iniciar el dia, el tiene que esperar que yo este listo para iniciarlo.
        sem_post(shm->residencial_listo);

        pthread_t hilos[HORAS_DIA][MAX_SOLICITUDES_RESIDENCIAL]; // depues puede cambiarse para lograr mejor eficiencia en espacio
        // 12 horas
        for (int i = 0; i < HORAS_DIA; i++) {

            int hora_actual = i + HORA_INICIO;
            int dia_actual = shm->dia_actual;
            
            if (!shm->simulacion_activa || proceso_terminado) break;
            
            // actualizar indice de informacion de hilos para acceder a informacion_hilos
            actualizar_nro_solicitudes(dia_actual - 1, i);
            
            // esperar señal de hora simulada
            esperar_hora_nodo_residencial(shm);

            printf("[Residencial] Día %d, Hora %d: Generando solicitudes...\n", 
                dia_actual, hora_actual);

            // generar hilos
            procesar_solicitud(dia_actual - 1, i, hilos);

        }

        sem_wait(shm->sem_nodo_dia_fin);

        cerrar_solicitudes_dia_actual(hilos);
    }
    
    // Limpieza
    desconectar_shm(shm);
    printf("[Residencial] Proceso terminado\n");
    
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
    printf("[Residencial] Señal recibida. Terminando proceso...\n");
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
    printf("[Residencial] Solicitudes generadas: ");
    for (int i = 0; i < HORAS_DIA; i++) {
        printf("%d ", solicitudes[i]);
    }
    printf("maximo: %d total: %d\n", max_solicitudes, total_solicitudes);
}

// dia_i y hora_i van 0-X
static void actualizar_nro_solicitudes(int dia_i, int hora_i) {
    numero_solicitudes[dia_i][hora_i] = solicitudes[hora_i];
}

// Procesar solicitudes de esta hora y dia (recibe indice de dia, hora y la tabla de hilos)
static void procesar_solicitud(int dia_i, int hora_i, pthread_t hilos[][MAX_SOLICITUDES_RESIDENCIAL]) {
    // Crear hilo para cada solicitud

    for (int i = 0; i < solicitudes[hora_i]; i++) {
        
        // Inicializamos su espacio
        InfoHilo *info = informacion_hilos[dia_i][hora_i][i];
        info->usuario_id = rand() % MAX_USERS;
        info->hilo_id = i;
        info->tiempo_espera = 0; // tiempo de espera en ms
        info->litros_consumidos = 0;
        info->op_realzada = NINGUNA;
        info->edo_final = DESCONOCIDO;
        
        
        pthread_create(&hilos[hora_i][i], NULL, hilo_solicitud, &informacion_hilos[dia_i][hora_i][i]);
    }

}

// Finalizar los hilos
void cerrar_solicitudes_dia_actual(pthread_t hilos[][MAX_SOLICITUDES_RESIDENCIAL]) {
    for (int hora = 0; hora < HORAS_DIA; hora++) {
        for (int i = 0; i < solicitudes[hora]; i++) {
            pthread_cancel(hilos[hora][i]); 
        }
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
    InfoHilo *info = (InfoHilo *)arg
    unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL); 
    pthread_cleanup_push(cleanup_handler, info);

    clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);

    // Generar número aleatorio para determinar la acción
    double prob = (double)rand_r(&seed) / RAND_MAX;
    
    printf("[Residencial] Solicitud usuario %d: ", info->usuario_id);
    
    int s = 0;
    if (prob < PROBABILIDAD_RESERVA) {
        s = rand_r(&seed) / RAND_MAX * 0.5f; // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
        usleep(1000000 * s);
        pthread_testcancel();
        printf("\tIntentando reservar nodo\n");
        esperar_asignacion(info->usuario_id);
    } else if (prob < 0.75) {
        s = rand_r(&seed) / RAND_MAX; // en cualquier momento se puede consultar presion
        usleep(1000000 * s);
        pthread_testcancel();
        printf("\tConsultando presión disponible\n");
        consultar_presion(info->usuario_id);
    } else { // en los primeros 45 minutos se cancelan solicitudes
        s = rand_r(&seed) / RAND_MAX * 0.75;
        usleep(1000000 * s);
        pthread_testcancel();   
        printf("\tCancelando solicitud\n");
        cancelar_solicitud(info->usuario_id);
    }

    pthread_cleanup_pop(1);
    return NULL
}

// Estado de esperar_asignacion -> reserva -> consumo
static void esperar_asignacion(InfoHilo *info) {

    pthread_rwlock_wrlock(mutex_nodos);
    
    while (1) {
        int nodo = obtener_nodo_disponible();
        if (nodo >= 0) {
            // Intentar reservar el nodo
            if (reservar_nodo(shm, nodo) == 0) {
                printf("[Residencial] Solicitud %d: Nodo %d reservado exitosamente\n", 
                    info->usuario_id, nodo);
                    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final)); // fin de la espera del hilo
                
                break;

            } else {
                continue;
            }
        }
        else {
            pthread_rwlock_unlock(mutex_nodos);

            lock_metricas(shm);
            shm->total_nodos_encontrados_ocupados++;
            unlock_metricas(shm);

            sem_wait(nodos_libres);
            pthread_rwlock_wrlock(mutex_nodos);
        }
    }
    
    pthread_rwlock_unlock(mutex_nodos);
    double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
    
    consumir_agua(info->usuario_id);
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);
}

// Consumir
static void consumir_agua(InfoHilo *info) {
    double consumo = generar_consumo_litros();
    
    lock_metricas(shm);
    shm->total_metros_cubicos += consumo / 1000.0;
    if (consumo > LIMITE_CONSUMO_CRITICO) {
        shm->senales_criticas++;
        printf("[Residencial] Solicitud %d: Consumo crítico de %.2f litros\n", 
                info->usuario_id, consumo);
    } 
    else 
    {
        shm->senales_estandar++;
    }
    unlock_metricas(shm);
    
    printf("[Residencial] Solicitud %d: Consumo  %.2f litros\n", 
           info->usuario_id, consumo);
}

// Estado de cancelar_solicitud -> generar_amonestacion | consumo
static void cancelar_solicitud(InfoHilo *info) {

    pthread_rwlock_wrlock(mutex_nodos);

    int nodo = tiene_reserva(info->usuario_id);
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

    pthread_rwlock_unlock(mutex_nodos);
}

static void generar_amonestacion(InfoHilo *info) {
    printf("[Residencial] Solicitud %d: Amonestada: $%.2f\n", 
        info->usuario_id, tarifa);
    lock_metricas(shm);
    shm->amonestaciones_digitales++;
    unlock_metricas(shm);

}

static void pagar_tarifa_excedente(InfoHilo *info, int nodo_id) {
    // Simular pago de tarifa (menor que industrial)
    // double tarifa = 25.0 + (rand() % 50); // $25-$75
    
    printf("[Residencial] Solicitud %d: Tarifa de excedente pagada: $%.2f\n", 
           info->usuario_id, tarifa);
    
    // Liberar el nodo
    liberar_nodo(shm, nodo_id);
    printf("[Residencial] Solicitud %d: Nodo %d liberado por pago\n", 
            info->usuario_id, nodo_id);
}

static void consultar_presion(InfoHilo *info) {
    // Simular consulta de presión

    pthread_rwlock_rdlock(mutex_nodos);
    int nodos_disponibles = 0;
    
    for (int i = 0; i < NUM_NODOS; i++) {
        if (leer_nodo(shm, i) == 0) {
            if (!shm->valvulas[i].ocupado) {
                nodos_disponibles++;
            }
            terminar_lectura_nodo(shm, i);
        }
    }

    pthread_rwlock_unlock(mutex_nodos);

    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final)); // fin de la espera del hilo
    double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);

    // Actualizar métricas
    lock_metricas(shm);
    shm->total_consultas_realizadas++;
    unlock_metricas(shm);
    
    printf("[Residencial] Solicitud %d: Presión disponible - %d/%d nodos libres\n", 
           info->usuario_id, nodos_disponibles, NUM_NODOS);

    
}


// Generar consumo entre 50 y 750 litros
static double generar_consumo_litros(void) {
    unsigned int seed = (unsigned int)pthread_self();

    return 50.0 + (rand_r(&seed) % 701);
}

// retorna el nodo reservado o -1
static int obtener_nodo_disponible(void) {
    // Buscar un nodo disponible
    for (int i = 0; i < NUM_NODOS; i++) {
        if (leer_nodo(shm, i) == 0) {
            if (!shm->valvulas[i].ocupado) {
                terminar_lectura_nodo(shm, i);
                return i;
            }
            terminar_lectura_nodo(shm, i);
        }
    }
    return -1; // No hay nodos disponibles
}

// retorna el nodo reservado o -1
static int tiene_reserva(InfoHilo *info) {
    for (int i = 0; i < NUM_NODOS; i++) {
        if (leer_nodo(shm, i) == 0) {
            if (!shm->valvulas[i].ocupado && shm->valvulas[i].usuario_id == usuario_id) {
                terminar_lectura_nodo(shm, i);
                return i;
            }
            terminar_lectura_nodo(shm, i);
        }
    }
    return -1;
}