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

int hilo_id = 1000;

static int max_solicitudes; // Maximas solicitudes que pueden haber en un dia
static int solicitudes[HORAS_DIA]; // Cada entrada indica el nro de solicitudes en esa hora


// Almacen de metadatos.
InfoHilo informacion_hilos[DIAS_SIMULACION][HORAS_DIA][MAX_SOLICITUDES_R];


// indice metadatos (para obtener el tamaño de informacion_hilos[_][_][i]).
int numero_solicitudes[DIAS_SIMULACION][HORAS_DIA] = {0};


pthread_t hilos[HORAS_DIA][MAX_SOLICITUDES_R]; // depues puede cambiarse para lograr mejor eficiencia en espacio

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

static void crear_solicitudes() {
    // asignar el maximo de solicitudes para el dia
    if (shm->usar_solicitudes_forzadas) {
        max_solicitudes = shm->solicitudes_forzadas_industrial * HORAS_DIA;
    } else {
        max_solicitudes = 250 - shm->max_solicitudes_residencial;
    }
    
    // Generar numero de solicitudes para el dia
    generar_solicitudes();
}

int main(void) {
    inicializar_y_configurar();

    // Conectar a memoria compartida
    shm = conectar_shm();
    if (shm == NULL) {
        fprintf(stderr, "[Industrial] Error: No se pudo conectar a memoria compartida\n");
        return EXIT_FAILURE;
    }
    sem_wait(&shm->activador_industrial);
    
    printf("[Industrial] (%06ld) Proceso iniciado (PID: %d)\n", obtener_timestamp_micros(), getpid());
    
    inicializar_semaforos();

    sem_wait(&shm->microseconds_sem);
    microseconds = shm->microseconds;
    sem_post(&shm->microseconds_sem);

    // Bucle principal de simulación
    while (shm->simulacion_activa && !proceso_terminado) {


        //printf("[Industrial] (%06ld) Esperando a residencial para escoger máximo...\n", obtener_timestamp_micros());
        sem_wait(&shm->sem_residencial_escoge_maximo); // esperar a residencial
        //printf("[Industrial] (%06ld) Residencial liberó el semáforo, continuando...\n", obtener_timestamp_micros());
        
        crear_solicitudes();
        
        // Inicializar la variable global de hilos
        memset(hilos, 0, sizeof(hilos));

        sem_post(&shm->sem_industrial_listo);

        set_ha_terminado_el_dia_actual(false);

        // 12 horas
        for (int i = 0; i < HORAS_DIA; i++) {

            int hora_actual = i + HORA_INICIO;
            int dia_actual = shm->dia_actual;
            
            if (!shm->simulacion_activa || proceso_terminado) break;
            
            // actualizar indice de informacion de hilos para acceder a informacion_hilos
            actualizar_nro_solicitudes(dia_actual - 1, i);
            
            // Avisar al líder que estamos listos para la hora
            sem_wait(&shm->sem_hora_empezada_industrial);
            
            if (!shm->simulacion_activa || proceso_terminado) break;
            
            printf("[Industrial] (%06ld) Día %d, Hora %d: Generando solicitudes...\n", 
                    obtener_timestamp_micros(), dia_actual, hora_actual);
                
            // generar hilos
            lanzar_hilos_solicitud(dia_actual - 1, i);
            
            
            // Esperar señal del líder para terminar procesos
            sem_wait(&shm->sem_nodo_industrial);
            sem_wait(&shm->sem_sync_industrial);
            
            if (i == HORAS_DIA - 1) 
            set_ha_terminado_el_dia_actual(true);
            
            set_ha_terminado_la_hora_actual(true);

            sem_post(&shm->sem_sync_residencial);


            //pthread_rwlock_unlock(&shm->mutex_nodos);
            
            // luego espero a que terminen
            for (int h = 0; h < solicitudes[i]; h++) {
                //printf("[Industrial] (%06ld) ESPERANDO CIERRE %d...\n", obtener_timestamp_micros(), informacion_hilos[dia_actual - 1][i][h].usuario_id);
                //for (int h_sig = h; h_sig < solicitudes[i]; h_sig++) {
                //    printf("[Industrial] (%06ld) RESTARIAN %d...\n", obtener_timestamp_micros(), informacion_hilos[dia_actual - 1][i][h_sig].usuario_id);
                //}
                pthread_join(hilos[i][h], NULL);
            }

            //printf("[Industrial] (%06ld) Día %d, Hora %d: PUDO LIBERAR...\n", obtener_timestamp_micros(), dia_actual, hora_actual);


            set_ha_terminado_la_hora_actual(false);

            // Avisar al líder que terminamos la hora
            sem_post(&shm->sem_nodo_industrial_listo_hora);


        }
        if (!shm->simulacion_activa || proceso_terminado) {
            break;
        }
        
        sem_wait(&shm->sem_nodo_industrial_dia_fin);

        set_ha_terminado_el_dia_actual(true);

        //sem_post(&shm->sem_industrial_listo);
    }

    // Generar reporte final antes de terminar
    mostrar_contenido(informacion_hilos, numero_solicitudes, "Industrial");

    // Limpieza 
    desconectar_shm(shm);

    
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
    srand(SEED_INDUSTRIAL);


}   

// CONFIG
static void manejador_senal(int sig) {
    (void)sig;
    proceso_terminado = 1;
/*     printf("[Industrial] (%06ld) Señal recibida. Terminando proceso...\n", obtener_timestamp_micros());
*/
    if (debug) mostrar_contenido(informacion_hilos, numero_solicitudes, "Industrial");
    
}

// [MIN_SOLICITUDES to MAX_SOLICITUDES] dependiendo de max_solicitudes
static void generar_solicitudes(void) {

    if (shm->usar_solicitudes_forzadas) {
        // Usar valores exactos desde memoria compartida
        int solicitudes_por_hora = shm->solicitudes_forzadas_industrial;
        for (int i = 0; i < HORAS_DIA; i++) {
            solicitudes[i] = solicitudes_por_hora;
        }
        return;
    }

    // Lógica actual con Poisson cuando no se usa forzado
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
    /* printf("[Industrial] (%06ld) Solicitudes generadas: ", obtener_timestamp_micros());
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
        info->usuario_id = rand() % MAX_USERS_I + USER_INDEX_I;
        info->hilo_id = hilo_id++;
        info->id_nodo = -1;  // No asignado aún
        // info->tiempo_espera = 0; // tiempo de espera en ms - no existe en la estructura
        info->m3_consumidos = 0;
        info->edo_solicitud = PENDIENTE;
        info->operacion = NINGUNA;
        
        
        pthread_create(&hilos[hora_i][i], NULL, hilo_solicitud, &informacion_hilos[dia_i][hora_i][i]);
    }

}

// Función que ejecutarán los hilos
static void* hilo_solicitud(void *arg) {
    InfoHilo *info = (InfoHilo *)arg;
    //unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL); 
    //pthread_cleanup_push(cleanup_handler, info);
    unsigned int seed = (unsigned int)(info->hilo_id + 1) * (unsigned int)info->usuario_id;

    // Generar número aleatorio para determinar la acción
    double prob = (double)generar_random_range(0,1, &seed);
    double h = (double)generar_random_range(0,1, &seed);

    // No espera si está aplazada.
    if (info->edo_solicitud == APLAZADA) {
        if (info->operacion == RESERVACION) {
            // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
            //usleep(microseconds * h * 0.5);
            //pthread_testcancel();
            clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);

            esperar_asignacion(info, "Industrial");
        } else if (info->operacion == CONSULTA_PRESION) {
            // en cualquier momento se puede consultar presion
            //usleep(microseconds * h * 0.98);
            //pthread_testcancel();
            clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);

            consultar_presion(info, "Industrial");
        } else { // en los primeros 45 minutos se cancelan solicitudes
            //usleep(microseconds * h * 0.75);
            //pthread_testcancel();   
            clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);

            cancelar_solicitud(info, "Industrial");
        }
    }
    else {

        if (prob < PROBABILIDAD_RESERVACION) {
            // las reservas suceden hasta casi media hora depues de iniciar el bloque horario
            usleep(microseconds * h * 0.5);
            //pthread_testcancel();
            clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);
    
            esperar_asignacion(info, "Industrial");
        } else if (prob < 0.75) {
            // en cualquier momento se puede consultar presion
            
            //pthread_testcancel();
        
            usleep(microseconds * h * 0.98);
            clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);
            consultar_presion(info, "Industrial");
        } else { // en los primeros 45 minutos se cancelan solicitudes
            usleep(microseconds * h * 0.75);
            //pthread_testcancel();   
            clock_gettime(CLOCK_MONOTONIC, &info->tiempo_espera_inicial);
    
            cancelar_solicitud(info, "Industrial");
        }
    }
    
    // Sincronizar salida para evitar mensajes mezclados
    if (debug){
        mostrar_estado_detalles_hilo(info, "Finalizando", "Industrial");
    }
    fflush(stdout);

    //pthread_cleanup_pop(1);

    if (get_edo_solicitud(info) != PROCESADA) {
        return NULL;
    }
    // aumentar el numero de consultas
    lock_metricas(shm);
    shm->total_consultas_realizadas++;
    unlock_metricas(shm);
    
    return NULL;
}
