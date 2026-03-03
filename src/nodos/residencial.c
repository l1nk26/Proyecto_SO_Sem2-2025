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
#define PROBABILIDAD_RESERVACION 0.5
#define LIMITE_CONSUMO_CRITICO 500.0
#define MAX_USERS 15
#define USER_INDEX 0
#define MIN_LITROS 50.0
#define MAX_LITROS 950.0

const bool debug = true;
int microseconds = 1000000; // 1s por defecto


// Variables globales
static volatile sig_atomic_t proceso_terminado = 0;
static MemoriaCompartida *shm = NULL;


static int max_solicitudes; // Maximas solicitudes que pueden haber en un dia
static int solicitudes[HORAS_DIA]; // Cada entrada indica el nro de solicitudes en esa hora


// Almacen de metadatos.
InfoHilo informacion_hilos[DIAS_SIMULACION][HORAS_DIA][MAX_SOLICITUDES_RESIDENCIAL];


// indice metadatos (para obtener el tamaño de informacion_hilos[_][_][i]).
int numero_solicitudes[DIAS_SIMULACION][HORAS_DIA] = {0};


// Hilos de ejecucion.
pthread_t hilos[HORAS_DIA][MAX_SOLICITUDES_RESIDENCIAL];


// Semáforos para protección de flags de estado
sem_t sem_dia;
sem_t sem_hora;

// Flags de estado
bool dia_terminado = false;
bool hora_terminado = false;

static int verificar_reserva(InfoHilo *info);
static int obtener_nodo_disponible(InfoHilo *info);
static void* hilo_solicitud(void *arg);
static void cancelar_solicitud(InfoHilo *info);
static void pagar_tarifa_excedente(InfoHilo *info, int nodo_id);
static void generar_amonestacion(InfoHilo *info);
static double generar_consumo_litros(InfoHilo *info);
static void manejador_de_finalizacion_exitosa(void *arg);
void manejador_de_finalizacion_temprana_dia_hora(void *arg);
void manejador_de_finalizacion_temprana(void *arg);
// [MIN_SOLICITUDES to MAX_SOLICITUDES] dependiendo de max_solicitudes
static void generar_solicitudes(void) {

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

}

static void inicializar_semaforos() {
    sem_init(&sem_dia, 0, 1);
    sem_init(&sem_hora, 0, 1);
}

// consultar si el dia termino
static bool get_ha_terminado_el_dia_actual() {
    bool x;
    sem_wait(&sem_dia);
    x = dia_terminado;
    sem_post(&sem_dia);
    return x;
}

static void set_ha_terminado_el_dia_actual(bool terminado) {
    sem_wait(&sem_dia);
    hora_terminado = terminado;
    sem_post(&sem_dia);
}

static bool get_ha_terminado_la_hora_actual() {
    bool x;
    sem_wait(&sem_hora);
    x = hora_terminado;
    sem_post(&sem_hora);
    return x;
}


static void set_ha_terminado_la_hora_actual(bool terminado) {
    sem_wait(&sem_hora);
    hora_terminado = terminado;
    sem_post(&sem_hora);
}

static void crear_solicitudes() {
    // asignar el maximo de solicitudes para el dia
    max_solicitudes = rand() % (MAX_SOLICITUDES_RESIDENCIAL - MIN_SOLICITUDES_RESIDENCIAL + 1) + MIN_SOLICITUDES_RESIDENCIAL;
    set_max_solicitudes_residencial(shm, max_solicitudes);
    
    // Generar numero de solicitudes para el dia
    generar_solicitudes();
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

// CONFIG
static void manejador_senal(int sig) {
    (void)sig;
    proceso_terminado = 1;
    printf("[Residencial] (%06ld) Señal recibida. Terminando proceso...\n", obtener_timestamp_ms());
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




// dia_i y hora_i van 0-X
static void actualizar_nro_solicitudes(int dia_i, int hora_i) {
    numero_solicitudes[dia_i][hora_i] = solicitudes[hora_i];
}

// Procesar solicitudes de esta hora y dia (recibe indice de dia, hora y la tabla de hilos)
static void lanzar_hilos_solicitud(int dia_i, int hora_i) {
    // Crear hilo para cada solicitud

    for (int i = 0; i < solicitudes[hora_i]; i++) {
        
        // Inicializamos su espacio
        InfoHilo *info = &informacion_hilos[dia_i][hora_i][i];
        info->usuario_id = rand() % MAX_USERS + USER_INDEX;
        info->hilo_id = i;
        info->m3_consumidos = 0;
        info->edo_solicitud = PENDIENTE;
        info->operacion = NINGUNA;
        
        pthread_create(&hilos[hora_i][i], NULL, hilo_solicitud, &informacion_hilos[dia_i][hora_i][i]);
    }

}

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



// Estado de esperar_asignacion -> reserva -> consumo
static void esperar_asignacion(InfoHilo *info) {

    set_operacion(info, RESERVACION);
    set_edo_solicitud(info, PENDIENTE);

    if (debug){
        mostrar_estado_detalles_hilo(info, "Esperando asignación", "Residencial");
    }

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

    manejador_de_finalizacion_exitosa(info);
    
    pthread_rwlock_unlock(&shm->mutex_nodos);
}

// Consumir
static void consumir_agua(InfoHilo *info) {
    double consumo = generar_consumo_litros(info);
    
    lock_metricas(shm);
    shm->total_metros_cubicos += consumo / 1000.0;
    if (consumo > LIMITE_CONSUMO_CRITICO) {
        shm->senales_criticas++;
        if (debug) {
            mostrar_estado_detalles_hilo(info, "Consumo crítico", "Residencial");
        }
    } 
    else 
    {
        shm->senales_estandar++;
        if (debug) {
            mostrar_estado_detalles_hilo(info, "Consumo estándar", "Residencial");
        }
    }
    unlock_metricas(shm);
}

// Estado de cancelar_solicitud -> generar_amonestacion | consumo
static void cancelar_solicitud(InfoHilo *info) {

    set_operacion(info, CANCELACION);
    set_edo_solicitud(info, PENDIENTE);

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
    }
    else {
        pagar_tarifa_excedente(info, nodo);
    }

    manejador_de_finalizacion_exitosa(info);

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
    set_operacion(info, CONSULTA_PRESION);
    set_edo_solicitud(info, PENDIENTE);

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
    
}

// Generar consumo entre 50 y 750 litros
static double generar_consumo_litros(InfoHilo *info) {
    return generar_random_range(MIN_LITROS, MAX_LITROS, &(info->hilo_id));
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

    sem_wait(&shm->microseconds_sem);
    microseconds = shm->microseconds;
    sem_post(&shm->microseconds_sem);

    // Bucle principal de simulación
    while (shm->simulacion_activa && !proceso_terminado) {

        crear_solicitudes();

        sem_post(&shm->sem_residencial_escoge_maximo); // para dar el permiso a residencial para que calcule su maximo de solicitudes

        // Inicializar la variable global de hilos
        memset(hilos, 0, sizeof(hilos));

        sem_post(&shm->sem_residencial_listo);

        set_ha_terminado_el_dia_actual(false);

        // 12 horas
        for (int i = 0; i < HORAS_DIA; i++) {

            int hora_actual = i + HORA_INICIO;
            int dia_actual = shm->dia_actual;
            
            if (!shm->simulacion_activa || proceso_terminado) break;
            
            // actualizar indice de informacion de hilos para acceder a informacion_hilos
            actualizar_nro_solicitudes(dia_actual - 1, i);
            
            // Avisar al líder que estamos listos para la hora
            sem_post(&shm->sem_nodo_residencial_listo_hora);
            
            
            printf("[Residencial] (%06ld) Día %d, Hora %d: Generando solicitudes...\n", 
            obtener_timestamp_ms(), dia_actual, hora_actual);

            // generar hilos
            lanzar_hilos_solicitud(dia_actual - 1, i);

            sem_wait(&shm->sem_nodo_residencial);
            

            if (i == HORAS_DIA - 1) 
            set_ha_terminado_el_dia_actual(true);
            
            set_ha_terminado_la_hora_actual(true);
            // Esperar a que todos los hilos terminen antes de avisar al líder 
            // (En esperar asignacion debo guardar el estado de los hilos que no pudieron ingresar al sistema para dejarlos para la siguiente hora)
            {}

            sem_post(&shm->sem_sync_industrial);

            for (int nodo = 0; nodo < NUM_NODOS; nodo++){
                liberar_nodo(shm, nodo);
            }
            
            
            //pthread_rwlock_unlock(&shm->mutex_nodos);
            int sem_val = 0;
            sem_getvalue(&shm->sem_nodos_libres, &sem_val);

            printf("[Residencial] (%06ld) NODOS LIBRES: %d...\n", obtener_timestamp_ms(), sem_val);
            
            // luego espero a que terminen
            for (int h = 0; h < solicitudes[i]; h++) {
                //printf("[Residencial] (%06ld) ESPERANDO CIERRE %d...\n", obtener_timestamp_ms(), informacion_hilos[dia_actual - 1][i][h].usuario_id);
                //for (int h_sig = h; h_sig < solicitudes[i]; h_sig++) {
                //    printf("[Residencial] (%06ld) RESTARIAN %d...\n", obtener_timestamp_ms(), informacion_hilos[dia_actual - 1][i][h_sig].usuario_id);
                //}
                pthread_join(hilos[i][h], NULL);
            }

            sem_getvalue(&shm->sem_nodos_libres, &sem_val);
            while (sem_val > 10) {
                sem_wait(&shm->sem_nodos_libres);
                sem_getvalue(&shm->sem_nodos_libres, &sem_val);
            }

            sem_getvalue(&shm->sem_nodos_libres, &sem_val);

            //printf("[Residencial] (%06ld) NODOS LIBRES 2: %d...\n", obtener_timestamp_ms(), sem_val);

            set_ha_terminado_la_hora_actual(false);
            // Avisar al líder que terminamos la hora
            sem_post(&shm->sem_nodo_residencial_listo_hora);
            

        }
        
        sem_wait(&shm->sem_nodo_residencial_dia_fin);

        set_ha_terminado_el_dia_actual(true);

        //sem_post(&shm->sem_residencial_listo);
    }
    
    // Limpieza
    desconectar_shm(shm);
    printf("[Residencial] (%06ld) Proceso terminado\n", obtener_timestamp_ms());
    
    return EXIT_SUCCESS;
}

// Función que ejecutarán los hilos
static void* hilo_solicitud(void *arg) {
    InfoHilo *info = (InfoHilo *)arg;
    //unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL); 

    //pthread_cleanup_push(manejador_de_finalizacion_temprana_dia_hora, info);

    clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);

    // Generar número aleatorio para determinar la acción
    double prob = (double)generar_random_range(0,1, &info->hilo_id);
    double h = (double)generar_random_range(0,1, &info->hilo_id);

    if (prob < PROBABILIDAD_RESERVACION) {
        // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
        usleep(microseconds * h * 0.5);
        //pthread_testcancel();

        esperar_asignacion(info);
    } else if (prob < 0.75) {
        // en cualquier momento se puede consultar presion
        usleep(microseconds * h * 0.98);
        //pthread_testcancel();

        consultar_presion(info);
    } else { // en los primeros 45 minutos se cancelan solicitudes
        usleep(microseconds * h * 0.75);
        //pthread_testcancel();   

        cancelar_solicitud(info);
    }

    // Sincronizar salida para evitar mensajes mezclados
    if (debug){
        mostrar_estado_detalles_hilo(info, "Finalizando", "Residencial");
    }
    fflush(stdout);

    //pthread_cleanup_pop(1);

    if (get_operacion(info) == DESCONOCIDO) {
        return NULL;
    }

    // aumentar el numero de consultas
    lock_metricas(shm);
    shm->total_consultas_realizadas++;
    unlock_metricas(shm);
    
    return NULL;
}