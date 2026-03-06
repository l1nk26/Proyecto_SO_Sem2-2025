#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <mqueue.h>
#include <sys/stat.h>
#include "auditor.h"
#include "ipc_shared.h"

// Variables globales del auditor
static MemoriaCompartida *shm = NULL;
static mqd_t mq_alertas = -1;
static pthread_t hilo_validacion_tid, hilo_calculo_tid;
static volatile int auditor_terminado = 0;

// Manejador de señales
static void manejador_senal(int sig) {
    (void)sig;
    auditor_terminado = 1;
}

void inicializar_auditor(void) {
    // Conectar a memoria compartida
    shm = conectar_shm();
    if (shm == NULL) {
        fprintf(stderr, "[Auditor] Error: No se pudo conectar a memoria compartida\n");
        exit(EXIT_FAILURE);
    }
    
    // Crear cola de mensajes para alertas
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;        // Máximo 10 mensajes en cola
    attr.mq_msgsize = sizeof(MensajeAlerta);
    attr.mq_curmsgs = 0;
    
    mq_alertas = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0644, &attr);
    if (mq_alertas == -1) {
        perror("[Auditor] mq_open");
        exit(EXIT_FAILURE);
    }
    
    printf("[Auditor] Inicializado (PID: %d)\n", getpid());
    printf("[Auditor] Cola de mensajes '%s' creada\n", MQ_NAME);
}

void cleanup_auditor(void) {
    printf("[Auditor] Limpiando recursos...\n");
    
    // Esperar a que terminen los hilos
    if (hilo_validacion_tid) {
        pthread_join(hilo_validacion_tid, NULL);
    }
    if (hilo_calculo_tid) {
        pthread_join(hilo_calculo_tid, NULL);
    }
    
    // Cerrar cola de mensajes
    if (mq_alertas != -1) {
        mq_close(mq_alertas);
        mq_unlink(MQ_NAME);
    }
    
    // Desconectar memoria compartida
    if (shm) {
        desconectar_shm(shm);
    }
}

void* hilo_validacion(void *arg) {
    (void)arg;
    MensajeAlerta msg;
    
    printf("[Auditor-Validación] Hilo iniciado, esperando alertas...\n");
    
    while (!auditor_terminado) {
        // Esperar mensaje de forma bloqueante
        ssize_t bytes_recibidos = mq_receive(mq_alertas, (char*)&msg, sizeof(MensajeAlerta), NULL);
        
        if (bytes_recibidos == -1) {
            if (errno == EINTR) continue; // Interrumpido por señal
            perror("[Auditor-Validación] mq_receive");
            break;
        }
        
        if (bytes_recibidos != sizeof(MensajeAlerta)) {
            fprintf(stderr, "[Auditor-Validación] Tamaño de mensaje incorrecto\n");
            continue;
        }
        
        printf("[Auditor-Validación] Alerta recibida: Nodo %d, %.2f litros\n", 
               msg.nodo_id, msg.litros_consumidos);
        
        procesar_alerta_critica(&msg);
    }
    
    printf("[Auditor-Validación] Hilo terminado\n");
    return NULL;
}

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
    }
    
    printf("[Auditor-Cálculo] Hilo terminado\n");
    return NULL;
}

void procesar_alerta_critica(const MensajeAlerta *msg) {
    // Decidir aleatoriamente si es crítico (aprueba) o estándar (multa)
    srand(time(NULL));
    int es_critico = rand() % 2; // 0=estándar, 1=crítico
    
    // Proteger actualización de métricas con mutex
    pthread_mutex_lock(&shm->mutex_metricas);
    
    if (es_critico) {
        shm->senales_criticas++;
        printf("[Auditor] Consumo CRÍTICO aprobado: Nodo %d, %.2f litros\n", 
               msg->nodo_id, msg->litros_consumidos);
    } else {
        shm->senales_estandar++;
        shm->amonestaciones_digitales++;
        printf("[Auditor] Consumo ESTÁNDAR multado: Nodo %d, %.2f litros\n", 
               msg->nodo_id, msg->litros_consumidos);
    }
    
    pthread_mutex_unlock(&shm->mutex_metricas);
}

void calcular_consumo_total_horario(void) {
    double consumo_total_horario = 0.0;
    
    // Sumar consumo de todos los nodos
    for (int i = 0; i < NUM_NODOS; i++) {
        // Aquí necesitarías acceder al consumo individual de cada nodo
        // Por ahora, asumimos que cada nodo guarda su consumo en alguna estructura
        // consumo_total_horario += shm->valvulas[i].consumo_horario;
    }
    
    // Convertir litros a metros cúbicos y acumular
    double metros_cubicos = consumo_total_horario / 1000.0;
    
    // Proteger actualización con mutex
    pthread_mutex_lock(&shm->mutex_metricas);
    shm->total_metros_cubicos += metros_cubicos;
    pthread_mutex_unlock(&shm->mutex_metricas);
}

int main(void) {
    printf("[Auditor] Iniciando proceso auditor...\n");
    
    // Configurar manejador de señales
    signal(SIGTERM, manejador_senal);
    signal(SIGINT, manejador_senal);
    
    // Inicializar auditor
    inicializar_auditor();
    
    // Crear hilos
    if (pthread_create(&hilo_validacion_tid, NULL, hilo_validacion, NULL) != 0) {
        perror("[Auditor] pthread_create validación");
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
    }
    
    printf("[Auditor] Simulación finalizada, limpiando...\n");
    cleanup_auditor();
    
    printf("[Auditor] Proceso terminado\n");
    return EXIT_SUCCESS;
}