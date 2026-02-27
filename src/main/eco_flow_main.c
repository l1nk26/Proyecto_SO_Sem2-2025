#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "ipc_shared.h"

// Prototipos
static int crear_memoria_compartida(MemoriaCompartida **shm);
static void inicializar_memoria(MemoriaCompartida *shm);
static void destruir_memoria_compartida(MemoriaCompartida *shm, int shm_fd);
static pid_t lanzar_proceso(const char *ejecutable);
static void mostrar_resultados(const MemoriaCompartida *shm);
static void manejador_senal(int sig);

// Variables globales para el manejador de señales
static volatile sig_atomic_t simulacion_terminada = 0;
static pid_t pids[4] = {0};

int main(void) {
    
    MemoriaCompartida *shm = NULL;
    int shm_fd;
    
    printf("[Líder] Iniciando Eco Flow Simulation...\n");
    
    // Crear memoria compartida
    shm_fd = crear_memoria_compartida(&shm);
    if (shm_fd < 0) {
        fprintf(stderr, "[Líder] Error al crear memoria compartida\n");
        return EXIT_FAILURE;
    }
    
    // Inicializar memoria compartida
    inicializar_memoria(shm);
    printf("[Líder] Memoria compartida creada e inicializada (%d nodos)\n", NUM_NODOS);
    
    // Configurar manejador de señales
    struct sigaction sa;
    sa.sa_handler = manejador_senal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Lanzar procesos de los otros desarrolladores
    printf("[Líder] Lanzando procesos de simulación...\n");
    
    pids[0] = lanzar_proceso("nodo_industrial");
    pids[1] = lanzar_proceso("nodo_residencial");
    pids[2] = lanzar_proceso("auditor_de_flujo");
    pids[3] = lanzar_proceso("monitoreo_de_presion");
    
    int procesos_lanzados = 0;
    for (int i = 0; i < 4; i++) {
        if (pids[i] > 0) { 
            procesos_lanzados++;
        } else {
            fprintf(stderr, "[Líder] Error: No se pudo lanzar el proceso %d\n", i);
        }
    }
    
    if (procesos_lanzados < 4) {
        fprintf(stderr, "[Líder] Error: No se pudo lanzar todos los procesos\n");
        destruir_memoria_compartida(shm, shm_fd);
        return EXIT_FAILURE;
    }
    
    printf("[Líder] %d procesos lanzados exitosamente\n", procesos_lanzados);
    
    // Bucle principal de simulación (30 "días")
    printf("[Líder] Iniciando simulación de %d días...\n", DIAS_SIMULACION);
    
    for (int dia = 1; dia <= DIAS_SIMULACION && !simulacion_terminada; dia++) {
        shm->dia_actual = dia;
        
        for (int hora = HORA_INICIO; hora <= HORA_FIN && !simulacion_terminada; hora++) {
            shm->hora_actual = hora;
            
            // Simular una "hora" de trabajo (1 segundo = 1 hora simulada)
            sleep(1);
            
            // Señalar a todos los procesos que pasó una hora
            sem_post(&shm->sem_nodo_industrial);
            sem_post(&shm->sem_nodo_residencial);
            sem_post(&shm->sem_auditor);
            sem_post(&shm->sem_monitoreo);

            // Mostrar progreso cada 6 horas
            if (hora % 6 == 0) {
                printf("[Líder] Día %d, Hora %d: Simulación activa...\n", dia, hora);
            }
        }
    }
    
    // Terminar simulación
    shm->simulacion_activa = false;
    printf("[Líder] Simulación completada. Enviando señales de terminación...\n");
    
    // Enviar SIGTERM a todos los procesos hijos
    for (int i = 0; i < 4; i++) {
        if (pids[i] > 0) {
            printf("[Líder] Enviando SIGTERM a PID %d\n", pids[i]);
            if (kill(pids[i], SIGTERM) < 0) {
                perror("[Líder] Error al enviar señal");
            }
        }
    }
    
    // Esperar a que todos los procesos terminen
    printf("[Líder] Esperando finalización de procesos...\n");
    for (int i = 0; i < 4; i++) {
        if (pids[i] > 0) {
            int status;
            pid_t result = waitpid(pids[i], &status, 0);
            if (result > 0) {
                if (WIFEXITED(status)) {
                    printf("[Líder] PID %d terminó con código %d\n", pids[i], WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("[Líder] PID %d terminó por señal %d\n", pids[i], WTERMSIG(status));
                }
            }
        }
    }
    
    // Mostrar resultados finales
    mostrar_resultados(shm);
    
    // Limpieza
    printf("[Líder] Limpiando memoria compartida...\n");
    destruir_memoria_compartida(shm, shm_fd);
    
    printf("[Líder] Simulación finalizada.\n");
    return EXIT_SUCCESS;
}

// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================

static int crear_memoria_compartida(MemoriaCompartida **shm) {
    // Crear bloque de memoria compartida
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[Líder] shm_open");
        return -1;
    }
    
    // Establecer tamaño
    if (ftruncate(shm_fd, sizeof(MemoriaCompartida)) < 0) {
        perror("[Líder] ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return -1;
    }
    
    // Mapear a memoria
    *shm = mmap(NULL, sizeof(MemoriaCompartida), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (*shm == MAP_FAILED) {
        perror("[Líder] mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return -1;
    }
    
    return shm_fd;
}

static void inicializar_memoria(MemoriaCompartida *shm) {
    // Inicializar nodos
    for (int i = 0; i < NUM_NODOS; i++) {
        shm->valvulas[i].id_nodo = i;
        shm->valvulas[i].ocupado = false;
        
        // Configurar rwlock con atributo PTHREAD_PROCESS_SHARED
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_rwlock_init(&shm->valvulas[i].rwlock_nodo, &attr);
        pthread_rwlockattr_destroy(&attr);
    }
    
    // Inicializar métricas
    shm->total_metros_cubicos = 0.0;
    shm->amonestaciones_digitales = 0;
    shm->senales_criticas = 0;
    shm->senales_estandar = 0;
    shm->tiempo_espera_total_ms = 0.0;
    shm->total_consultas_realizadas = 0;
    shm->total_nodos_encontrados_ocupados = 0;
    
    // Inicializar estado
    shm->dia_actual = 1;
    shm->hora_actual = HORA_INICIO;
    shm->simulacion_activa = true;
    
    // Inicializar mutex de métricas con PTHREAD_PROCESS_SHARED
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->mutex_metricas, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    // Inicializar semáforos para sincronización por hora (pshared=1, valor inicial=0)
    // Los procesos hijos harán sem_wait() y el main hará sem_post() cada hora
    sem_init(&shm->sem_nodo_industrial, 1, 0);
    sem_init(&shm->sem_nodo_residencial, 1, 0);
    sem_init(&shm->sem_auditor, 1, 0);
    sem_init(&shm->sem_monitoreo, 1, 0);
}

static void destruir_memoria_compartida(MemoriaCompartida *shm, int shm_fd) {
    // Destruir rwlocks
    for (int i = 0; i < NUM_NODOS; i++) {
        pthread_rwlock_destroy(&shm->valvulas[i].rwlock_nodo);
    }

    // Destruir semáforos
    sem_destroy(&shm->sem_nodo_industrial);
    sem_destroy(&shm->sem_nodo_residencial);
    sem_destroy(&shm->sem_auditor);
    sem_destroy(&shm->sem_monitoreo);

    // Destruir mutex
    pthread_mutex_destroy(&shm->mutex_metricas);
    
    // Desmapear
    if (munmap(shm, sizeof(MemoriaCompartida)) < 0) {
        perror("[Líder] munmap");
    }
    
    // Cerrar descriptor
    close(shm_fd);
    
    // Eliminar objeto de memoria compartida
    if (shm_unlink(SHM_NAME) < 0) {
        perror("[Líder] shm_unlink");
    }
}

static pid_t lanzar_proceso(const char *ejecutable) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("[Líder] fork");
        return -1;
    } else if (pid == 0) {
        // Proceso hijo
        execlp(ejecutable, ejecutable, NULL);
        
        // Si execlp retorna, hubo un error
        fprintf(stderr, "[Líder] Error al ejecutar %s: %s\n", ejecutable, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Proceso padre
    printf("[Líder] Lanzado %s (PID: %d)\n", ejecutable, pid);
    return pid;
}

static void mostrar_resultados(const MemoriaCompartida *shm) {
    printf("\n");
    printf("========================================\n");
    printf("      RESULTADOS DE LA SIMULACIÓN       \n");
    printf("========================================\n");
    printf("Total metros cúbicos consumidos: %.2f m³\n", shm->total_metros_cubicos);
    printf("Amonestaciones digitales emitidas: %d\n", shm->amonestaciones_digitales);
    printf("Señales críticas enviadas: %d\n", shm->senales_criticas);
    printf("Señales estándar enviadas: %d\n", shm->senales_estandar);
    printf("Consultas realizadas al sistema: %d\n", shm->total_consultas_realizadas);
    printf("Nodos encontrados ocupados: %d\n", shm->total_nodos_encontrados_ocupados);
    
    if (shm->total_consultas_realizadas > 0) {
        double eficiencia = shm->tiempo_espera_total_ms / shm->total_consultas_realizadas;
        printf("Tiempo promedio de espera: %.2f ms\n", eficiencia);
    }
    
    printf("========================================\n");
}

static void manejador_senal(int sig) {
    (void)sig;
    simulacion_terminada = 1;
    printf("\n[Líder] Señal recibida. Terminando simulación...\n");
}