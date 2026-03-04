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


static int max_solicitudes; // Maximas solicitudes que pueden haber en un dia
static int solicitudes[HORAS_DIA]; // Cada entrada indica el nro de solicitudes en esa hora


// Almacen de metadatos.
InfoHilo informacion_hilos[DIAS_SIMULACION][HORAS_DIA][MAX_SOLICITUDES_R];


// indice metadatos (para obtener el tamaño de informacion_hilos[_][_][i]).
int numero_solicitudes[DIAS_SIMULACION][HORAS_DIA] = {0};


// Hilos de ejecucion.
pthread_t hilos[HORAS_DIA][MAX_SOLICITUDES_R];




// Prototipos CONFIG
static void manejador_senal(int sig);
static void inicializar_y_configurar(void);

// Funciones de simulación
static void generar_solicitudes(void);

static void crear_solicitudes();
static void actualizar_nro_solicitudes(int dia_i, int hora_i);
static void lanzar_hilos_solicitud(int dia_i, int hora_i);
static void* hilo_solicitud(void *arg);

// pasar de a a b
void intercambiar_estados(InfoHilo *a, InfoHilo *b) {
    InfoHilo temp = *a;
    //*a = *b;
    *b = temp;
}

void recuperar_solicitudes_aplazadas(int *recuperados, int dia_i, int hora_i) {
    *recuperados = 0;
    for (int i = 0; i < numero_solicitudes[dia_i][hora_i]; i++) {
        if (informacion_hilos[dia_i][hora_i][i].edo_solicitud == APLAZADA) {
            intercambiar_estados(&informacion_hilos[dia_i][hora_i][i], &informacion_hilos[dia_i][hora_i + 1][*recuperados]);
            (*recuperados)++;
        }
    }
    numero_solicitudes_aplazadas = 0;
}

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

static void crear_solicitudes() {
    // asignar el maximo de solicitudes para el dia
    max_solicitudes = rand() % (MAX_SOLICITUDES_R - MIN_SOLICITUDES_R + 1) + MIN_SOLICITUDES_R;
    set_max_solicitudes_residencial(shm, max_solicitudes);
    
    // Generar numero de solicitudes para el dia
    generar_solicitudes();
}


// CONFIG
static void manejador_senal(int sig) {
    (void)sig;
    proceso_terminado = 1;
    printf("[Residencial] (%06ld) Señal recibida. Terminando proceso...\n", obtener_timestamp_ms());
    if (debug) mostrar_contenido(informacion_hilos, numero_solicitudes, "Residencial");

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
    srand(SEED_RESIDENCIAL);


}   


// dia_i y hora_i van 0-X
static void actualizar_nro_solicitudes(int dia_i, int hora_i) {
    numero_solicitudes[dia_i][hora_i] = solicitudes[hora_i];
}

// Procesar solicitudes de esta hora y dia (recibe indice de dia, hora y la tabla de hilos)
static void lanzar_hilos_solicitud(int dia_i, int hora_i) {
    // Crear hilo para cada solicitud
    int recuperados = 0;
    if (numero_solicitudes_aplazadas > 0) {
        recuperar_solicitudes_aplazadas(&recuperados, dia_i, hora_i - 1);
    }

    for (int i = 0; i < recuperados; i++) {
        pthread_create(&hilos[hora_i][i], NULL, hilo_solicitud, &informacion_hilos[dia_i][hora_i][i]);
    }
    solicitudes[hora_i] += recuperados;

    for (int i = recuperados; i < solicitudes[hora_i]; i++) {
        
        // Inicializamos su espacio
        InfoHilo *info = &informacion_hilos[dia_i][hora_i][i];
        info->usuario_id = rand() % MAX_USERS_R + USER_INDEX_R;
        info->hilo_id = i;
        info->m3_consumidos = 0;
        info->edo_solicitud = PENDIENTE;
        info->operacion = NINGUNA;
        
        pthread_create(&hilos[hora_i][i], NULL, hilo_solicitud, &informacion_hilos[dia_i][hora_i][i]);
    }

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

    // No espera si está aplazada.
    if (info->edo_solicitud == APLAZADA) {
        if (prob < PROBABILIDAD_RESERVACION) {
            // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
            //usleep(microseconds * h * 0.5);
            //pthread_testcancel();

            esperar_asignacion(info, "Residencial");
        } else if (prob < 0.75) {
            // en cualquier momento se puede consultar presion
            //usleep(microseconds * h * 0.98);
            //pthread_testcancel();

            consultar_presion(info, "Residencial");
        } else { // en los primeros 45 minutos se cancelan solicitudes
            //usleep(microseconds * h * 0.75);
            //pthread_testcancel();   

            cancelar_solicitud(info, "Residencial");
        }
    }
    else {

        if (prob < PROBABILIDAD_RESERVACION) {
            // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
            usleep(microseconds * h * 0.5);
            //pthread_testcancel();
    
            esperar_asignacion(info, "Residencial");
        } else if (prob < 0.75) {
            // en cualquier momento se puede consultar presion
            usleep(microseconds * h * 0.98);
            //pthread_testcancel();
    
            consultar_presion(info, "Residencial");
        } else { // en los primeros 45 minutos se cancelan solicitudes
            usleep(microseconds * h * 0.75);
            //pthread_testcancel();   
    
            cancelar_solicitud(info, "Residencial");
        }
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