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
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
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
enum estados_de_solicitud {PENDIENTE = 0, PROCESADA, CANCELADA, APLAZADA, DESCONOCIDO, DUPLICADA1, DUPLICADA2};

const int nombre_operacion_count = 4;
const int nombre_estado_count = 7;

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
    "DESCONOCIDO     ",
    "DUPLICADA1      ",
    "DUPLICADA2      "
};

typedef struct {
    unsigned int usuario_id; // iniciado en 0 | Usuario referenciado
    unsigned int hilo_id; // iniciado en 0 | Indice de hilo
    int id_nodo; // ID del nodo asignado (-1 si no asignado)
    struct timespec tiempo_espera_inicial;
    struct timespec tiempo_espera_final;
    double m3_consumidos;
    unsigned int edo_solicitud;
    unsigned int operacion;
} InfoHilo;

typedef struct {
    unsigned int dias;
    unsigned int horas;
    unsigned int minutos;
    unsigned int segundos;
} DT;

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

static DT micros_to_DT(unsigned int microseconds_to_DT, unsigned int microseconds_eq_hour);

static void inicializar_semaforos();
static bool get_ha_terminado_el_dia_actual();
static bool get_ha_terminado_la_hora_actual();
static void set_ha_terminado_el_dia_actual(bool valor);
static void set_ha_terminado_la_hora_actual(bool valor);

void manejador_de_finalizacion_temprana_dia_hora(void *arg);
void manejador_de_finalizacion_temprana(void *arg);
static void manejador_de_finalizacion_exitosa(void *arg);
static int obtener_nodo_disponible();

static void consultar_presion(InfoHilo *info, const char *nombre_proceso);

static void cancelar_solicitud(InfoHilo *info, const char *nombre_proceso);
static void generar_amonestacion();
static bool pagar_tarifa_excedente(InfoHilo *info, int nodo_id);

static void esperar_asignacion(InfoHilo *info, const char *nombre_proceso);
static void consumir_agua(InfoHilo *info, const char *nombre_proceso);
static double generar_consumo_litros(InfoHilo *info, const char *nombre_proceso);
static int verificar_reserva(InfoHilo *info);

// full hardcoded este ajajja
static DT micros_to_DT(unsigned int microseconds_to_DT, unsigned int microseconds_eq_hour) {
    DT dt;
    dt.dias = 0;
    dt.horas = 0;
    dt.minutos = 0;
    dt.segundos = 0;

    unsigned int dia = microseconds_eq_hour * 24;
    unsigned int hora = microseconds_eq_hour;
    unsigned int minuto = hora / 60;
    unsigned int segundo = minuto / 60;

    // dias
    while (microseconds_to_DT > dia) {
        dt.dias++;
        microseconds_to_DT -= dia;

    }

    // hora
    while (microseconds_to_DT > hora) {
        dt.horas++;
        microseconds_to_DT -= hora;
        if (dt.horas >= 24) {
            dt.horas = 0;
            dt.dias++;
        }
    }

    // minuto
    while (microseconds_to_DT > minuto) {
        dt.minutos++;
        microseconds_to_DT -= minuto;
        if (dt.minutos >= 60) {
            dt.minutos = 0;
            dt.horas++;
            if (dt.horas >= 24) {
                dt.horas = 0;
                dt.dias++;
            }
        }
    }

    // segundo
    while (microseconds_to_DT > segundo) {
        dt.segundos++;
        microseconds_to_DT -= segundo;
        if (dt.segundos >= 60) {
            dt.segundos = 0;
            dt.minutos++;
            if (dt.minutos >= 60) {
                dt.minutos = 0;
                dt.horas++;
                if (dt.horas >= 24) {
                    dt.horas = 0;
                    dt.dias++;
                }
            }
        }
    }

    return dt;
}

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


static struct timespec inicio;
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void inicializar_tiempo_base(void) {
    clock_gettime(CLOCK_MONOTONIC, &inicio);
}

// Función para obtener timestamp en ms desde el inicio del proceso
static long obtener_timestamp_micros(void) {
    struct timespec ahora;
    
    // Garantiza ejecución única y thread-safe de inicializar_tiempo_base
    pthread_once(&once_control, inicializar_tiempo_base);
    
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    
    int64_t segundos = (int64_t)(ahora.tv_sec - inicio.tv_sec);
    int64_t nanosegundos = (int64_t)(ahora.tv_nsec - inicio.tv_nsec);
    
    if (nanosegundos < 0) {
        segundos -= 1;
        nanosegundos += 1000000000LL;
    }
    
    return (segundos * 1000000LL) + (nanosegundos / 1000LL);
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

int64_t calcular_tiempo_transcurrido_micros_int(const struct timespec *inicio, const struct timespec *fin) {
    int64_t segundos = (int64_t)(fin->tv_sec - inicio->tv_sec);
    int64_t nanosegundos = (int64_t)(fin->tv_nsec - inicio->tv_nsec);

    if (nanosegundos < 0) {
        segundos -= 1;
        nanosegundos += 1000000000LL;
    }
    return (segundos * 1000000LL) + (nanosegundos / 1000LL);
}

// Genera un número aleatorio en el rango [min, max]
int generar_random_range_int(int min, int max, unsigned int *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return min + (*seed / 0x7fffffff) * (max - min);    
}

// Genera un número aleatorio en el rango [min, max]
double generar_random_range(double min, double max, unsigned int *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return min + (*seed / (double)0x7fffffff) * (max - min);
}

void mostrar_estado_detalles_hilo(InfoHilo *info, const char *mensaje, const char *proceso) {
    printf("[%s] (%010ld) %s Hilo %d. Estado: operacion=%s, estado de solicitud=%s\n", 
           proceso, obtener_timestamp_micros(), mensaje, info->usuario_id, nombre_operacion[info->operacion], nombre_estado[info->edo_solicitud]);
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
    int64_t tiempo_espera = calcular_tiempo_transcurrido_micros_int(&(datos->tiempo_espera_inicial), &(datos->tiempo_espera_final));
    
    lock_metricas(shm);
    shm->tiempo_espera_total_micros += tiempo_espera;
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
    int64_t tiempo_espera = calcular_tiempo_transcurrido_micros_int(&(datos->tiempo_espera_inicial), &(datos->tiempo_espera_final));
    
    lock_metricas(shm);
    shm->tiempo_espera_total_micros += tiempo_espera;
    unlock_metricas(shm);

    if (get_edo_solicitud(datos) == DUPLICADA1 ||
        get_edo_solicitud(datos) == DUPLICADA2) {
        return;
    }
    else {
        set_edo_solicitud(datos, DESCONOCIDO);
    }
}

static void manejador_de_finalizacion_exitosa(void *arg) {

    if (arg == NULL) {
        return;
    }

    InfoHilo *info = (InfoHilo *)arg;
    clock_gettime(CLOCK_MONOTONIC, &(info->tiempo_espera_final));
    int64_t tiempo_espera = calcular_tiempo_transcurrido_micros_int(&(info->tiempo_espera_inicial), &(info->tiempo_espera_final));
    
    lock_metricas(shm);
    shm->tiempo_espera_total_micros += tiempo_espera;
    unlock_metricas(shm);

    info->edo_solicitud = PROCESADA;
}

// v1
// retorna el nodo reservado o -1
static int obtener_nodo_disponible() {
    
    for (int i = 0; i < NUM_NODOS; i++) {
        pthread_rwlock_rdlock(&shm->valvulas[i].rwlock_nodo);
        if (!shm->valvulas[i].ocupado) {
            pthread_rwlock_unlock(&shm->valvulas[i].rwlock_nodo);
            return i;
        }
        pthread_rwlock_unlock(&shm->valvulas[i].rwlock_nodo);
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

    //pthread_rwlock_rdlock(&shm->mutex_nodos);

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        //pthread_rwlock_unlock(&shm->mutex_nodos);
        //sem_post(&shm->sem_nodos_libres);

        manejador_de_finalizacion_temprana_dia_hora(info);

        return;
    }
    
    // simular que hacemos algo
    int nodos_disponibles = 0;
    for (int i = 0; i < NUM_NODOS; i++) {
        pthread_rwlock_rdlock(&shm->valvulas[i].rwlock_nodo);
        if (!shm->valvulas[i].ocupado) {
            nodos_disponibles++;
        }
        pthread_rwlock_unlock(&shm->valvulas[i].rwlock_nodo);
    }
    
    //pthread_rwlock_unlock(&shm->mutex_nodos);

    manejador_de_finalizacion_exitosa(info);

    mostrar_estado_detalles_hilo(info, "Consulta de presión completada", nombre_proceso);
    
}

// v1
// Estado de cancelar_solicitud -> generar_amonestacion | consumo
static void cancelar_solicitud(InfoHilo *info, const char *nombre_proceso) {

    set_operacion(info, CANCELACION);
    set_edo_solicitud(info, PENDIENTE);

    mostrar_estado_detalles_hilo(info, "Solicitando cancelación", nombre_proceso);


    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        manejador_de_finalizacion_temprana_dia_hora(info);
        return;
    }

    int nodo = verificar_reserva(info);
    
    if (nodo < 0) {
        generar_amonestacion();
        mostrar_estado_detalles_hilo(info, "Cancelación rechazada - Sin reserva", nombre_proceso);
    }
    else {
        if (!pagar_tarifa_excedente(info, nodo)) {
            // Error al pagar tarifa excedente
            generar_amonestacion();
            mostrar_estado_detalles_hilo(info, "Cancelación rechazada - Error al pagar tarifa excedente", nombre_proceso);
            /* manejador_de_finalizacion_temprana(info);
            return; */
        }
        else {
            mostrar_estado_detalles_hilo(info, "Cancelación exitosa", nombre_proceso);
        }
    }

    manejador_de_finalizacion_exitosa(info);
}


// Generar amonestacion
static void generar_amonestacion() {
    lock_metricas(shm);
    shm->amonestaciones_digitales++;
    unlock_metricas(shm);
}


static bool pagar_tarifa_excedente(InfoHilo *info, int nodo_id) {
    if (liberar_nodo(shm, nodo_id, info->usuario_id) == -1) {
        set_edo_solicitud(info, DUPLICADA2);
        return false;
    }
    return true;
}


// v1
// Estado de esperar_asignacion -> reserva -> consumo
static void esperar_asignacion(InfoHilo *info, const char *nombre_proceso) {

    set_operacion(info, RESERVACION);
    set_edo_solicitud(info, PENDIENTE);


    mostrar_estado_detalles_hilo(info, "Esperando asignación", nombre_proceso);
    
    sem_wait(&shm->sem_nodos_libres);

    pthread_rwlock_wrlock(&shm->mutex_nodos);
    int nodo = obtener_nodo_disponible();

    // Teoricamente no deberia de ser -1, pero por si acaso
    if (nodo == -1) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana(info);
        return;
    }

    if (verificar_reserva(info) != -1) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        set_edo_solicitud(info, DUPLICADA1);
        manejador_de_finalizacion_temprana(info);
        return;
    }

    if (get_ha_terminado_la_hora_actual() || get_ha_terminado_el_dia_actual()) {
        pthread_rwlock_unlock(&shm->mutex_nodos);
        sem_post(&shm->sem_nodos_libres);
        manejador_de_finalizacion_temprana_dia_hora(info);      

        return;
    }
    
    
    pthread_rwlock_wrlock(&shm->valvulas[nodo].rwlock_nodo);
    if (reservar_nodo(shm, nodo, info->usuario_id) == -1) {
        sem_post(&shm->sem_nodos_libres);
        if (!(shm == NULL || nodo < 0 || nodo >= NUM_NODOS)) {
            set_edo_solicitud(info, DUPLICADA2);
        }
        pthread_rwlock_unlock(&shm->mutex_nodos);
        pthread_rwlock_unlock(&shm->valvulas[nodo].rwlock_nodo);
        manejador_de_finalizacion_temprana(info);
        return;
    }
    
    
    info->id_nodo = nodo;  // Asignar el ID del nodo reservado
    consumir_agua(info, nombre_proceso);
    pthread_rwlock_unlock(&shm->mutex_nodos);
    pthread_rwlock_unlock(&shm->valvulas[nodo].rwlock_nodo);


    manejador_de_finalizacion_exitosa(info);
    
}

// Consumir
static void consumir_agua(InfoHilo *info, const char *nombre_proceso) {

    double consumo = generar_consumo_litros(info, nombre_proceso);
    // Actualizar estado del nodo específico en lugar de métricas globales
    if (info->id_nodo >= 0 && info->id_nodo < NUM_NODOS) {
        // Reservar nodo para escritura
        //if (reservar_nodo(shm, info->id_nodo, info->hilo_id) == 0) {
            // Actualizar consumo horario del nodo

            shm->valvulas[info->id_nodo].consumo_horario += consumo / 1000.0;
            
            // Actualizar métricas del hilo
            info->m3_consumidos += consumo / 1000.0;
            
            // Mostrar estado según nivel de consumo
            if ((shm->valvulas[info->id_nodo].consumo_horario) * 1000.0 > LIMITE_CONSUMO_CRITICO) {
                
                pthread_mutex_lock(&shm->mutex_consumo_critico);
                shm->nodo_consumo_critico_id = info->id_nodo;
                pthread_mutex_unlock(&shm->mutex_consumo_critico);
                
                mostrar_estado_detalles_hilo(info, "Consumo crítico", nombre_proceso);

                sem_post(&shm->sem_auditor_listas);
            } else {
                mostrar_estado_detalles_hilo(info, "Consumo estándar", nombre_proceso);
            }
            
            
        //} else {
        //    printf("[%s] Error: No se pudo reservar nodo %d para consumo\n", 
        //           nombre_proceso, info->id_nodo);
        //}
    } else {
        printf("[%s] Error: ID de nodo inválido %d\n", nombre_proceso, info->id_nodo);
    }
}


// v1
// Generar consumo entre 50 y 750 litros
static double generar_consumo_litros(InfoHilo *info, const char *nombre_proceso) {
    unsigned int seed = (unsigned int)(info->hilo_id + 1) ^ (unsigned int)info->usuario_id ^ 
                       (unsigned int)time(NULL) ^ (unsigned int)(info->tiempo_espera_inicial.tv_nsec);
    if (strcmp(nombre_proceso, "Residencial") == 0) {
        return generar_random_range(MIN_LITROS_R, MAX_LITROS_R, &seed);
    }
    return generar_random_range(MIN_LITROS_I, MAX_LITROS_I, &seed);
}

// v1
// retorna el nodo reservado o -1
static int verificar_reserva(InfoHilo *info) {
    for (int i = 0; i < NUM_NODOS; i++) {
        pthread_rwlock_rdlock(&shm->valvulas[i].rwlock_nodo);
        if (shm->valvulas[i].ocupado && (unsigned int)shm->valvulas[i].usuario_id == (unsigned int)info->usuario_id) {
            pthread_rwlock_unlock(&shm->valvulas[i].rwlock_nodo);
            return i;
        }
        pthread_rwlock_unlock(&shm->valvulas[i].rwlock_nodo);
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
    int64_t suma_tiempo_espera = 0;
    double suma_m3 = 0.0;
    int conteo_operacion[nombre_operacion_count];
    int conteo_estado[nombre_estado_count];

    // inicializar
    for (int i = 0; i < nombre_estado_count; i++) {
        conteo_estado[i] = 0;
    }
    for (int i = 0; i < nombre_operacion_count; i++) {
        conteo_operacion[i] = 0;
    }

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
                double espera = calcular_tiempo_transcurrido_micros_int(&p->tiempo_espera_inicial, &p->tiempo_espera_final);

                // Acumular estadísticas
                total_solicitudes++;
                suma_tiempo_espera += espera;
                suma_m3 += p->m3_consumidos;

                unsigned int op = p->operacion;
                unsigned int edo = p->edo_solicitud;

                // Verificar rangos para evitar desbordamiento de arreglos
                if (op < nombre_operacion_count) conteo_operacion[op]++;
                if (edo < nombre_estado_count) conteo_estado[edo]++;

                // Traducir valores de enumeración a cadenas
                const char *op_str = (op < nombre_operacion_count) ? nombre_operacion[op] : "DESCONOCIDO";
                const char *edo_str = (edo < nombre_estado_count) ? nombre_estado[edo] : "DESCONOCIDO";

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

    DT dt_promedio = micros_to_DT(suma_tiempo_espera, shm->microseconds);

    if (total_solicitudes > 0) {
/*         fprintf(log, "Tiempo de espera total: D/H/M/S %2d:%2d:%2d:%2d\n",
                dt_total.dias, dt_total.horas, dt_total.minutos, dt_total.segundos);*/
        fprintf(log, "Tiempo de espera total (microsegundos): %.6f \n",
                    (double)(suma_tiempo_espera)); 
        fprintf(log, "Tiempo de espera: D/H/M/S %2d:%2d:%2d:%2d\n",
                dt_promedio.dias, dt_promedio.horas, dt_promedio.minutos, dt_promedio.segundos);
        fprintf(log, "Metros cúbicos promedio: %.6f\n",
                (double)(suma_m3));
    } else {
        fprintf(log, "No hay solicitudes registradas.\n");
    }

    // Conteo por operación
    fprintf(log, "\nConteo por tipo de operación:\n");
    for (int i = 0; i < nombre_operacion_count; i++) {
        fprintf(log, "  %-20s : %d\n", nombre_operacion[i], conteo_operacion[i]);
    }

    // Conteo por estado
    fprintf(log, "\nConteo por estado de solicitud:\n");
    for (int i = 0; i < nombre_estado_count; i++) {
        fprintf(log, "  %-20s : %d\n", nombre_estado[i], conteo_estado[i]);
    }

    fprintf(log, "\n=== ESTADÍSTICAS POR OPERACIÓN ===\n");
    
    // Reiniciar contadores para cálculos por operación
    long long conteo_por_op[nombre_operacion_count];
    double suma_tiempo_por_op[nombre_operacion_count];
    double suma_m3_por_op[nombre_operacion_count];
        
    // inicializar
    for (int i = 0; i < nombre_operacion_count; i++) {
        conteo_por_op[i] = 0;
        suma_tiempo_por_op[i] = 0.0;
        suma_m3_por_op[i] = 0.0;
    }


    // Recorrer nuevamente para acumular por operación
    for (int dia = 0; dia < DIAS_SIMULACION; dia++) {
        for (int hora = 0; hora < HORAS_DIA; hora++) {
            int num_sol = numero_solicitudes[dia][hora];
            if (num_sol == 0) continue;
                
            for (int i = 0; i < num_sol; i++) {
                InfoHilo *p = &info[dia][hora][i];
                    
                double espera = calcular_tiempo_transcurrido_micros_int(&p->tiempo_espera_inicial, &p->tiempo_espera_final);
                unsigned int op = p->operacion;
                    
                if (op < nombre_operacion_count) {
                    conteo_por_op[op]++;
                    suma_tiempo_por_op[op] += espera;
                    suma_m3_por_op[op] += p->m3_consumidos;
                }
            }
        }
    }
        
    // Mostrar estadísticas por operación
    for (int i = 0; i < nombre_operacion_count; i++) {
        if (conteo_por_op[i] > 0) {
            DT dt_promedio_op = micros_to_DT(suma_tiempo_por_op[i] / conteo_por_op[i], shm->microseconds);
            double m3_promedio_op = suma_m3_por_op[i] / conteo_por_op[i];
                
            fprintf(log, "\n--- %s ---\n", nombre_operacion[i]);
            fprintf(log, "  Total solicitudes: %lld\n", conteo_por_op[i]);
            fprintf(log, "  Tiempo promedio: D/H/M/S %2d:%2d:%2d:%2d\n",
                    dt_promedio_op.dias, dt_promedio_op.horas, 
                    dt_promedio_op.minutos, dt_promedio_op.segundos);
            fprintf(log, "Tiempo total (microsegundos): %.6f \n",
                    (double)(suma_tiempo_por_op[i])); 
            fprintf(log, "  Tiempo promedio (micros): %.6f\n", 
                    suma_tiempo_por_op[i] / conteo_por_op[i]);
            fprintf(log, "  m³ promedio: %.6f\n", m3_promedio_op);
            fprintf(log, "  m³ total: %.6f\n", suma_m3_por_op[i]);
        }
    }

    fclose(log);
    printf("Reporte generado en %s_log.txt \n", log_filename);
}