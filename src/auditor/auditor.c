#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include "auditor.h"
#include "ipc_shared.h"
#include "ipc_utils.h"

// Variables globales del auditor
static MemoriaCompartida *shm = NULL;
static pthread_t hilo_alertas_tid, hilo_calculo_tid;
static volatile int auditor_terminado = 0;
static MensajeAlerta alerta_actual;




// Manejador de señales
static void manejador_senal(int sig) {
    (void)sig;
    auditor_terminado = 1;
    // Despertar hilos bloqueados
    sem_post(&shm->sem_auditor_listas);
    sem_post(&shm->sem_auditor);
    printf("[Auditor] Terminando Simulacion por señal..\n");
}

void inicializar_auditor(void) {
    // Conectar a memoria compartida
    shm = conectar_shm();
    if (shm == NULL) {
        fprintf(stderr, "[Auditor] Error: No se pudo conectar a memoria compartida\n");
        exit(EXIT_FAILURE);
    }
    
    //semilla de random
    srand(2);

    printf("[Auditor] Inicializado (PID: %d)\n", getpid());
}

void cleanup_auditor(void) {
    printf("[Auditor] Limpiando recursos...\n");

    //terminar los bucles infinitos
    auditor_terminado = 1;
    
    // Despertar hilos bloqueados
    sem_post(&shm->sem_auditor_listas);
    sem_post(&shm->sem_auditor);

    // Esperar a que terminen los hilos
    if (hilo_alertas_tid) pthread_join(hilo_alertas_tid, NULL);
    if (hilo_calculo_tid) pthread_join(hilo_calculo_tid, NULL);
    
    // Desconectar memoria compartida
    if (shm) {
        desconectar_shm(shm);
    }

    printf("[Auditor] Recursos Completamente Limpiados, Auditor Finalizado.\n");
}

// Calcula el consumo por hora
void* hilo_calculo_horario(void *arg) {
    (void)arg;
    
    printf("[Auditor-Cálculo] Hilo iniciado, esperando por hora...\n");
    
    while (!auditor_terminado && shm->simulacion_activa) {
        // Esperar señal de que pasó una hora
        sem_wait(&shm->sem_auditor);
        
        if (auditor_terminado || !shm->simulacion_activa) break;
        
        calcular_consumo_total_horario();
        
        printf("[Auditor-Cálculo] Hora %d procesada. Total: %.2f m³\n", 
               shm->hora_actual, shm->total_metros_cubicos);
        
        sem_post(&shm->sem_auditor_terminado);
    }
    
    printf("[Auditor-Cálculo] Hilo terminado\n");
    return NULL;
}

void* hilo_procesar_alertas(void *arg) {
    (void)arg;

    printf("[Auditor-Alertas] Hilo iniciado, esperando alertas...\n");
    
    while (!auditor_terminado && shm->simulacion_activa) {
        // Esperar semáforo de alertas
        sem_wait(&shm->sem_auditor_listas);

        if (auditor_terminado || !shm->simulacion_activa) break;
        
        // buscamos cual nodo consumio mas de 500 litros

        // Voy A SUPONER QUE SOLO VA HABER UNO A LA VEZ QUE ESTA SOLICITANDO EL CONSUMO DE 500 litros
        int nodo_id_critico;
        
        pthread_mutex_lock(&shm->mutex_consumo_critico);
        nodo_id_critico = shm->nodo_consumo_critico_id;
        pthread_mutex_unlock(&shm->mutex_consumo_critico); 
        // Lectura concurrente de consumo_horario
        
        if (nodo_id_critico != -1) {
            if (leer_nodo(shm, nodo_id_critico) == 0) {
                    
                alerta_actual.nodo_id = nodo_id_critico;
                alerta_actual.litros_consumidos = (shm->valvulas[nodo_id_critico].consumo_horario)*1000.0;
                alerta_actual.usuario_id = shm->valvulas[nodo_id_critico].usuario_id;
                alerta_actual.es_critico = true; 
                    
                terminar_lectura_nodo(shm, nodo_id_critico);
            }
            

            // Procesar alerta actual
            if(alerta_actual.es_critico)
                procesar_alerta_critica(&alerta_actual);

            alerta_actual.es_critico = false;

             // Limpiar para próxima alerta
            pthread_mutex_lock(&shm->mutex_consumo_critico);
            shm->nodo_consumo_critico_id = -1;  // ← Resetear
            pthread_mutex_unlock(&shm->mutex_consumo_critico);

            printf("[Auditor-Alertas] Alerta procesada: Nodo %d, %.2f litros\n", 
                alerta_actual.nodo_id, alerta_actual.litros_consumidos);
        }
    }
    
    printf("[Auditor-Alertas] Hilo terminado\n");
    return NULL;
}

void procesar_alerta_critica(const MensajeAlerta *msg) {
    // Decidir aleatoriamente si es crítico (aprueba) o estándar (multa)

    int es_critico = rand() % 2; 
    // 50% de probabilidad de que sea crítico (1)
    //int es_critico = (rand() % 100 < 50) ? 1 : 0; // 0=estándar, 1=crítico

    // Proteger actualización de métricas con mutex
    pthread_mutex_lock(&shm->mutex_metricas);
    
    if (es_critico) {
        shm->senales_criticas++;
        printf("[Auditor] Consumo CRÍTICO aprobado: Nodo %d, %.2f litros, El Usuario id, %d\n", 
               msg->nodo_id, msg->litros_consumidos, msg->usuario_id);
    } else {
        shm->senales_estandar++;
        //sshm->amonestaciones_digitales++;
        printf("[Auditor] Consumo ESTÁNDAR multado: Nodo %d, %.2f litros, El Usuario id, %d\n", 
               msg->nodo_id, msg->litros_consumidos, msg->usuario_id);
    }
    
    pthread_mutex_unlock(&shm->mutex_metricas);
}

void calcular_consumo_total_horario() {
    double consumo_total_horario = 0.0;
    
    // Sumar consumo de todos los nodos
    for (int i = 0; i < NUM_NODOS; i++) {

        // Lectura concurrente de consumo_horario
        if (leer_nodo(shm, i) == 0) {
            consumo_total_horario += shm->valvulas[i].consumo_horario;
            terminar_lectura_nodo(shm, i);
        }
    }
    
    // Proteger actualización con mutex
    pthread_mutex_lock(&shm->mutex_metricas);
    shm->total_metros_cubicos += consumo_total_horario;
    pthread_mutex_unlock(&shm->mutex_metricas);
} 

int main(void) {
    printf("[Auditor] Iniciando proceso auditor...\n");
    
    // Configurar manejador de señales
    signal(SIGTERM, manejador_senal);
    signal(SIGINT, manejador_senal);
    
    // Inicializar auditor
    inicializar_auditor();
    
    /* // Simular validaciones directas sin hilo
    while (!auditor_terminado && shm->simulacion_activa) {
        sleep(1);  // Esperar 1 segundo
        
        //sem_wait(&shm->sem_auditor_terminado);

        // Simular recepción de alerta directa
        MensajeAlerta msg_simulada;
        msg_simulada.nodo_id = rand() % NUM_NODOS;
        msg_simulada.litros_consumidos = 400 + (rand() % 200);  // 400-600 litros
        msg_simulada.tipo_proceso = rand() % 2;
        msg_simulada.pid_proceso = getpid();
        
        procesar_alerta_critica(&msg_simulada);
    } */

    // Crear hilos
    if (pthread_create(&hilo_alertas_tid, NULL, hilo_procesar_alertas, NULL) != 0) {
        perror("[Auditor] pthread_create alertas");
        cleanup_auditor();
        return EXIT_FAILURE;
    }
 
    if (pthread_create(&hilo_calculo_tid, NULL, hilo_calculo_horario, NULL) != 0) {
        perror("[Auditor] pthread_create cálculo");
        cleanup_auditor();
        return EXIT_FAILURE;
    }
    
    // Esperar fin de simulación
    while (!auditor_terminado && shm->simulacion_activa) {
        sleep(1);

        // Depuración
        printf("[Auditor] Estado: terminado=%d, activa=%d, dia=%d, hora=%d\n",
            auditor_terminado, shm->simulacion_activa, 
            shm->dia_actual, shm->hora_actual);
    }
    
    printf("[Auditor] Simulación finalizada, limpiando...\n");
    cleanup_auditor();
    
    printf("[Auditor] Proceso terminado\n");
    return EXIT_SUCCESS;
}