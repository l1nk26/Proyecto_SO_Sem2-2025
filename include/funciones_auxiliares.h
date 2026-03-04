#pragma once
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "ipc_utils.h"
#include "ipc_shared.h"

#define SEED_INDUSTRIAL 12345
#define SEED_RESIDENCIAL 67890

#define HORAS_DIA 12
#define PROBABILIDAD_RESERVACION 0.5
#define LIMITE_CONSUMO_CRITICO 500.0

#define MAX_SOLICITUDES_R 200
#define MIN_SOLICITUDES_R 50

#define MAX_USERS_R 15
#define USER_INDEX_R 0

#define MAX_USERS_I 12
#define USER_INDEX_I 100

#define MIN_LITROS_R 50.0
#define MAX_LITROS_R 950.0

#define MIN_LITROS_I 50.0
#define MAX_LITROS_I 950.0

enum operacion {NINGUNA = 0, RESERVACION, CANCELACION, CONSULTA_PRESION};
enum estados_de_solicitud {PENDIENTE = 0, PROCESADA, CANCELADA, APLAZADA, DESCONOCIDO, RECUPERADA};

static const char* nombre_operacion[] = {
    "NINGUNA         ",
    "RESERVACION     ",
    "CANCELACION     ",
    "CONSULTA_PRESION"
};

static const char* nombre_estado[] = {
    "PENDIENTE       ",
    "PROCESADA       ",
    "CANCELADA       ",
    "APLAZADA        ",
    "DESCONOCIDO     "
};

typedef struct {
    unsigned int usuario_id; // iniciado en 0 | Usuario referenciado
    unsigned int hilo_id; // iniciado en 0 | Indice de hilo
    struct timespec tiempo_espera_inicial;
    struct timespec tiempo_espera_final;
    double m3_consumidos;
    unsigned int edo_solicitud;
    unsigned int operacion;
} InfoHilo;

const bool debug = true;
int microseconds = 1000000; // 1s por defecto
int numero_solicitudes_aplazadas = 0;

// Variables globales
static volatile sig_atomic_t proceso_terminado = 0;
static MemoriaCompartida *shm = NULL;

// Semáforos para protección de flags de estado
sem_t sem_dia;
sem_t sem_hora;

// Flags de estado
bool dia_terminado = false;
bool hora_terminado = false;

static void inicializar_semaforos();
static bool get_ha_terminado_el_dia_actual();
static bool get_ha_terminado_la_hora_actual();
static void set_ha_terminado_el_dia_actual(bool valor);
static void set_ha_terminado_la_hora_actual(bool valor);

void manejador_de_finalizacion_temprana_dia_hora(void *arg);
void manejador_de_finalizacion_temprana(void *arg);
static void manejador_de_finalizacion_exitosa(void *arg);
static int obtener_nodo_disponible(InfoHilo *info);

static void consultar_presion(InfoHilo *info, const char *nombre_proceso);

static void cancelar_solicitud(InfoHilo *info, const char *nombre_proceso);
static void generar_amonestacion(InfoHilo *info);
static void pagar_tarifa_excedente(InfoHilo *info, int nodo_id);

static void esperar_asignacion(InfoHilo *info, const char *nombre_proceso);
static void consumir_agua(InfoHilo *info, const char *nombre_proceso);
static double generar_consumo_litros(InfoHilo *info, const char *nombre_proceso);
static int verificar_reserva(InfoHilo *info);

static void inicializar_semaforos() {
    sem_init(&sem_dia, 0, 1);
    sem_init(&sem_hora, 0, 1);
}

static void set_ha_terminado_el_dia_actual(bool valor) {
    sem_wait(&sem_dia);
    dia_terminado = valor;
    sem_post(&sem_dia);
}

bool get_ha_terminado_el_dia_actual() {
    bool valor;
    sem_wait(&sem_dia);
    valor = dia_terminado;
    sem_post(&sem_dia);
    return valor;
}

void set_ha_terminado_la_hora_actual(bool valor) {
    sem_wait(&sem_hora);
    hora_terminado = valor;
    sem_post(&sem_hora);
}

bool get_ha_terminado_la_hora_actual() {
    bool valor;
    sem_wait(&sem_hora);
    valor = hora_terminado;
    sem_post(&sem_hora);
    return valor;
}

// Función para obtener timestamp en ms desde el inicio del proceso
static long obtener_timestamp_ms(void) {
    static struct timespec inicio;
    static int inicializado = 0;
    struct timespec ahora;
    
    if (!inicializado) {
        clock_gettime(CLOCK_MONOTONIC, &inicio);
        inicializado = 1;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    long segundos = ahora.tv_sec - inicio.tv_sec;
    long nanosegundos = ahora.tv_nsec - inicio.tv_nsec;
    
    if (nanosegundos < 0) {
        segundos -= 1;
        nanosegundos += 1000000000L;
    }
    
    return segundos * 1000 + nanosegundos / 1000000;
}

// Implementacion
static double factorial(int n) {
    if (n <= 1) return 1;
    double result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

static int poisson_ppf(double lambda, double p) {
    double L = exp(-lambda);
    int x = 0;
    double acum = 0;
    
    while (acum < p) {
        acum += L * pow(lambda, x) / factorial(x);
        x++;
    }
    
    return x - 1;
}

double calcular_tiempo_transcurrido(const struct timespec *inicio, const struct timespec *fin) {
    long segundos = fin->tv_sec - inicio->tv_sec;
    long nanosegundos = fin->tv_nsec - inicio->tv_nsec;

    if (nanosegundos < 0) {
        segundos -= 1;
        nanosegundos += 1000000000L;
    }
    return (double)segundos + (double)nanosegundos / 1000000000.0;
}

// Genera un número aleatorio en el rango [min, max]
int generar_random_range_int(int min, int max, unsigned int *seed) {
    return min + (rand_r(seed) % (max - min + 1));
}

// Genera un número aleatorio en el rango [min, max]
double generar_random_range(double min, double max, unsigned int *seed) {
    return min + (rand_r(seed) / (double)(RAND_MAX)) * (max - min);
}

void mostrar_estado_detalles_hilo(InfoHilo *info, const char *mensaje, const char *proceso) {
    printf("[%s] (%06ld) %s Hilo %d. Estado: operacion=%d, estado de solicitud=%d\n", 
           proceso, obtener_timestamp_ms(), mensaje, info->usuario_id, info->operacion, info->edo_solicitud);
}

void set_operacion(InfoHilo *info, int operacion) {
    info->operacion = operacion;
}

void set_edo_solicitud(InfoHilo *info, int edo_solicitud) {
    info->edo_solicitud = edo_solicitud;
}

int get_operacion(InfoHilo *info) {
    return info->operacion;
}

int get_edo_solicitud(InfoHilo *info) {
    return info->edo_solicitud;
}


// SIMULACION

// Limpieza antes de matar los hilos
void manejador_de_finalizacion_temprana_dia_hora(void *arg) {
    InfoHilo *datos = (InfoHilo *)arg;

    // operaciones comunes
    clock_gettime(CLOCK_MONOTONIC, &(datos->tiempo_espera_final));
    double tiempo_espera = calcular_tiempo_transcurrido(&(datos->tiempo_espera_inicial), &(datos->tiempo_espera_final)) * 1000;
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);

    if (get_ha_terminado_el_dia_actual()) {
        set_edo_solicitud(datos, CANCELADA);
        //datos->edo_op_realizada = NINGUNA;
    }
    else {
        set_edo_solicitud(datos, APLAZADA);
        //datos->edo_op_realizada = NINGUNA;
        numero_solicitudes_aplazadas++;
    }

}

void manejador_de_finalizacion_temprana(void *arg) {
    InfoHilo *datos = (InfoHilo *)arg;

    // operaciones comunes
    clock_gettime(CLOCK_MONOTONIC, &(datos->tiempo_espera_final));
    double tiempo_espera = calcular_tiempo_transcurrido(&(datos->tiempo_espera_inicial), &(datos->tiempo_espera_final)) * 1000;
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);

    if (get_ha_terminado_el_dia_actual()) {
        set_edo_solicitud(datos, DESCONOCIDO);
        //datos->edo_op_realizada = NINGUNA;
    }
    else {
        set_edo_solicitud(datos, DESCONOCIDO);
        //datos->edo_op_realizada = NINGUNA;
    }
}

static void manejador_de_finalizacion_exitosa(void *arg) {

    if (arg == NULL) {
        return;
    }

    InfoHilo *info = (InfoHilo *)arg;
    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final));
    double tiempo_espera = calcular_tiempo_transcurrido(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final)) * 1000;
    
    lock_metricas(shm);
    shm->tiempo_espera_total_ms += tiempo_espera;
    unlock_metricas(shm);

    info->edo_solicitud = PROCESADA;
}

// No bloquea semaforos
// retorna el nodo reservado o -1
static int obtener_nodo_disponible(InfoHilo *info) {
    for (int i = 0; i < NUM_NODOS; i++) {
        if (!shm->valvulas[i].ocupado) {
            return i;
        }
    }

    lock_metricas(shm);
    shm->total_nodos_encontrados_ocupados++;
    unlock_metricas(shm);

    return -1; // No hay nodos disponibles
}

static void consultar_presion(InfoHilo *info, const char *nombre_proceso) {
    // Simular consulta de presión - usar rdlock global
    set_operacion(info, CONSULTA_PRESION);
    set_edo_solicitud(info, PENDIENTE);

    mostrar_estado_detalles_hilo(info, "Consultando presión", nombre_proceso);   

    pthread_rwlock_rdlock(&shm->mutex_nodos);

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);

        manejador_de_finalizacion_temprana_dia_hora(info);

        return;
    }
    
    int nodos_disponibles = 0;
    for (int i = 0; i < NUM_NODOS; i++) {
        if (!shm->valvulas[i].ocupado) {
            nodos_disponibles++;
        }
    }
    
    pthread_rwlock_unlock(&shm->mutex_nodos);

    manejador_de_finalizacion_exitosa(info);

    mostrar_estado_detalles_hilo(info, "Consulta de presión completada", nombre_proceso);
    
}


// Estado de cancelar_solicitud -> generar_amonestacion | consumo
static void cancelar_solicitud(InfoHilo *info, const char *nombre_proceso) {

    set_operacion(info, CANCELACION);
    set_edo_solicitud(info, PENDIENTE);

    mostrar_estado_detalles_hilo(info, "Solicitando cancelación", nombre_proceso);

    pthread_rwlock_wrlock(&shm->mutex_nodos);

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);

        manejador_de_finalizacion_temprana_dia_hora(info);

        return;
    }

    int nodo = verificar_reserva(info);
    
    if (nodo < 0) {
        generar_amonestacion(info);
        mostrar_estado_detalles_hilo(info, "Cancelación rechazada - Sin reserva", nombre_proceso);
    }
    else {
        pagar_tarifa_excedente(info, nodo);
        mostrar_estado_detalles_hilo(info, "Cancelación exitosa", nombre_proceso);
    }

    manejador_de_finalizacion_exitosa(info);

    pthread_rwlock_unlock(&shm->mutex_nodos);


}


// Generar amonestacion
static void generar_amonestacion(InfoHilo *info) {
    lock_metricas(shm);
    shm->amonestaciones_digitales++;
    unlock_metricas(shm);
}


static void pagar_tarifa_excedente(InfoHilo *info, int nodo_id) {
    liberar_nodo(shm, nodo_id);
}



// Estado de esperar_asignacion -> reserva -> consumo
static void esperar_asignacion(InfoHilo *info, const char *nombre_proceso) {

    set_operacion(info, RESERVACION);
    set_edo_solicitud(info, PENDIENTE);


    mostrar_estado_detalles_hilo(info, "Esperando asignación", nombre_proceso);
    

    pthread_rwlock_wrlock(&shm->mutex_nodos);

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana_dia_hora(info);

        return;
    }

    pthread_rwlock_unlock(&shm->mutex_nodos);
    
    sem_wait(&shm->sem_nodos_libres);

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana_dia_hora(info);      

        return;
    }
    
    
    pthread_rwlock_wrlock(&shm->mutex_nodos);

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana_dia_hora(info);

        return;
    }

    if (verificar_reserva(info) != -1) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana(info);
        return;
    }

    int nodo = obtener_nodo_disponible(info);
    if (nodo == -1) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana(info);
        return;
    }

    reservar_nodo(shm, nodo, info->usuario_id);
    consumir_agua(info, nombre_proceso);
    manejador_de_finalizacion_exitosa(info);
    
    pthread_rwlock_unlock(&shm->mutex_nodos);
}

// Consumir
static void consumir_agua(InfoHilo *info, const char *nombre_proceso) {
    double consumo = generar_consumo_litros(info, nombre_proceso);
    
    lock_metricas(shm);
    shm->total_metros_cubicos += consumo / 1000.0;
    info->m3_consumidos += consumo / 1000.0;
    if (consumo > LIMITE_CONSUMO_CRITICO) {
        shm->senales_criticas++;
        
        mostrar_estado_detalles_hilo(info, "Consumo crítico", nombre_proceso);
        
    } 
    else 
    {
        shm->senales_estandar++;
        
        mostrar_estado_detalles_hilo(info, "Consumo estándar", nombre_proceso);
        
    }
    unlock_metricas(shm);
}


// Generar consumo entre 50 y 750 litros
static double generar_consumo_litros(InfoHilo *info, const char *nombre_proceso) {
    if (strcmp(nombre_proceso, "Residencial") == 0) {
        return generar_random_range(MIN_LITROS_R, MAX_LITROS_R, &(info->hilo_id));
    }
    return generar_random_range(MIN_LITROS_I, MAX_LITROS_I, &(info->hilo_id));
}


// No bloquea semaforos
// retorna el nodo reservado o -1
static int verificar_reserva(InfoHilo *info) {
    for (int i = 0; i < NUM_NODOS; i++) {
        if (shm->valvulas[i].ocupado && shm->valvulas[i].usuario_id == info->usuario_id) {
            return i;
        }
    }
    return -1;
}




void mostrar_contenido(InfoHilo info[DIAS_SIMULACION][HORAS_DIA][MAX_SOLICITUDES_R], int numero_solicitudes[DIAS_SIMULACION][HORAS_DIA], const char* nombre_proceso) {
    
    char log_filename[256];
    snprintf(log_filename, sizeof(log_filename), "logs/%s_log.txt", nombre_proceso);
    FILE *log = fopen(log_filename, "w");
    if (!log) {
        perror(strcat("Error al abrir ", log_filename));
        return;
    }

    // Variables para estadísticas globales
    long long total_solicitudes = 0;
    double suma_tiempo_espera = 0.0;
    double suma_m3 = 0.0;
    int conteo_operacion[4] = {0};        // Índices: 0..3 (NINGUNA..CONSULTA_PRESION)
    int conteo_estado[5] = {0};            // Índices: 0..4 (PENDIENTE..DESCONOCIDO)

    fprintf(log, "=== REPORTE DE SIMULACIÓN ===\n\n");
    fprintf(log, "Formato: [Día Hora] Usuario Hilo  TiempoEspera(s)  m3  Operación  Estado\n\n");

    // Recorrido día por día y hora por hora
    for (int dia = 0; dia < DIAS_SIMULACION; dia++) {
        for (int hora = 0; hora < HORAS_DIA; hora++) {
            int num_sol = numero_solicitudes[dia][hora];
            if (num_sol == 0) continue;   // Saltar horas sin solicitudes

            fprintf(log, "--- Día %d, Hora %02d ---  (%d solicitudes)\n", dia, hora, num_sol);

            for (int i = 0; i < num_sol; i++) {
                InfoHilo *p = &info[dia][hora][i];

                // Calcular tiempo de espera
                double espera = calcular_tiempo_transcurrido(&p->tiempo_espera_inicial, &p->tiempo_espera_final);

                // Acumular estadísticas
                total_solicitudes++;
                suma_tiempo_espera += espera;
                suma_m3 += p->m3_consumidos;

                unsigned int op = p->operacion;
                unsigned int edo = p->edo_solicitud;

                // Verificar rangos para evitar desbordamiento de arreglos
                if (op < 4) conteo_operacion[op]++;
                if (edo < 5) conteo_estado[edo]++;

                // Traducir valores de enumeración a cadenas
                const char *op_str = (op < 4) ? nombre_operacion[op] : "DESCONOCIDO";
                const char *edo_str = (edo < 5) ? nombre_estado[edo] : "DESCONOCIDO";

                // Imprimir registro en el archivo
                fprintf(log, "  [%02d:%02d:%02d]  %8u  %6u  %12.6f  %6.2f  %-15s  %-10s\n",
                        dia, hora, i,
                        p->usuario_id,
                        p->hilo_id,
                        espera,
                        p->m3_consumidos,
                        op_str,
                        edo_str);
            }
            fprintf(log, "\n");
        }
    }

    // Imprimir estadísticas generales
    fprintf(log, "\n=== ESTADÍSTICAS GLOBALES ===\n");
    fprintf(log, "Total de solicitudes: %lld\n", total_solicitudes);

    if (total_solicitudes > 0) {
        fprintf(log, "Tiempo de espera promedio: %.6f segundos\n",
                suma_tiempo_espera / total_solicitudes);
        fprintf(log, "Metros cúbicos promedio: %.6f\n",
                suma_m3 / total_solicitudes);
    } else {
        fprintf(log, "No hay solicitudes registradas.\n");
    }

    // Conteo por operación
    fprintf(log, "\nConteo por tipo de operación:\n");
    for (int i = 0; i < 4; i++) {
        fprintf(log, "  %-20s : %d\n", nombre_operacion[i], conteo_operacion[i]);
    }

    // Conteo por estado
    fprintf(log, "\nConteo por estado de solicitud:\n");
    for (int i = 0; i < 5; i++) {
        fprintf(log, "  %-20s : %d\n", nombre_estado[i], conteo_estado[i]);
    }

    fclose(log);
    printf("Reporte generado en %s_log.txt \n", log_filename);
}