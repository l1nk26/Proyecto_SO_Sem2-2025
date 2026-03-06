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
#include "ipc_utils.h"
#include <stdbool.h>
#include <funciones_auxiliares.h>

// Prototipos
static int crear_memoria_compartida(MemoriaCompartida **shm);
static void inicializar_memoria(MemoriaCompartida *shm);
static void destruir_memoria_compartida(MemoriaCompartida *shm, int shm_fd);
static pid_t lanzar_proceso(const char *ejecutable);
static void mostrar_resultados(const MemoriaCompartida *shm);
static void manejador_senal(int sig);

// Variables globales para el manejador de señales
static volatile sig_atomic_t simulacion_terminada = 0;
static pid_t pids[4] = {0, 0, 0, 0};

int main(int argc, char** argv) {

    int microseconds = 1000000;
    if (argc > 2) {
        if (strcmp(argv[1], "--microseconds") == 0 || strcmp(argv[1], "-m") == 0  ) {
            microseconds = atoi(argv[2]);
        }
        if (microseconds < 10000) {
            printf("[Orquestador] El valor de microseconds debe ser mayor o igual a 10000\n");
            return EXIT_FAILURE;
        }
    }
    
    MemoriaCompartida *shm = NULL;
    int shm_fd;
    
    printf("[Orquestador] Iniciando Eco Flow Simulation...\n");
    
    // Crear memoria compartida
    shm_fd = crear_memoria_compartida(&shm);
    if (shm_fd < 0) {
        fprintf(stderr, "[Orquestador] Error al crear memoria compartida\n");
        return EXIT_FAILURE;
    }
    
    // Inicializar memoria compartida
    inicializar_memoria(shm);
    set_microseconds(shm, microseconds);  // 1000000 microseconds si --fast, 0 si normal
    printf("[Orquestador] Memoria compartida creada e inicializada (%d nodos)\n", NUM_NODOS);
    
    // Configurar manejador de señales
    struct sigaction sa;
    sa.sa_handler = manejador_senal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Lanzar procesos de los otros desarrolladores
    printf("[Orquestador] Lanzando procesos de simulación...\n");
    
    pids[0] = lanzar_proceso("residencial");
    pids[1] = lanzar_proceso("industrial");
    // pids[2] = lanzar_proceso("auditor");
    pids[3] = 0;
    // pids[3] = lanzar_proceso("monitor");



    
/* int procesos_lanzados = 0;
    for (int i = 0; i < 1; i++) { // Solo verificar el proceso residencial
        if (pids[i] > 0) { 
            procesos_lanzados++;
        } else {
            fprintf(stderr, "[Orquestador] Error: No se pudo lanzar el proceso %d\n", i);
        }
    }
    
    if (procesos_lanzados < 1) { // Cambiado a 1 para prueba
        fprintf(stderr, "[Orquestador] Error: No se pudo lanzar todos los procesos\n");
        destruir_memoria_compartida(shm, shm_fd);
        return EXIT_FAILURE;
    } */
    
    //printf("[Orquestador] %d procesos lanzados exitosamente\n", procesos_lanzados);
    
    // Bucle principal de simulación (30 "días")
    printf("[Orquestador] Iniciando simulación de %d días...\n", DIAS_SIMULACION);
    sleep(2);
    sem_post(&shm->activador_industrial);
    sem_post(&shm->activador_residencial);

    for (int dia = 1; dia <= DIAS_SIMULACION && !simulacion_terminada; dia++) {
    
        sem_wait(&shm->sem_residencial_listo); // espera antes de iniciar el dia.
        sem_wait(&shm->sem_industrial_listo);
        shm->dia_actual = dia;

        for (int hora = HORA_INICIO; hora < HORA_FIN && !simulacion_terminada; hora++) {
            shm->hora_actual = hora;
            
            // Resetear consumo horario de todos los nodos al inicio de cada hora
            for (int nodo = 0; nodo < NUM_NODOS; nodo++) {
                shm->valvulas[nodo].consumo_horario = 0.0;
            }
            
            // Esperar a que los nodos estén listos para la hora
            sem_wait(&shm->sem_nodo_residencial_listo_hora);
            sem_wait(&shm->sem_nodo_industrial_listo_hora);
            


            for (int i = 0; i < 60; i++) {
                usleep(microseconds / 60); // 1 minuto
                shm->minuto_actual = i;
            }

            sem_post(&shm->sem_nodo_residencial);
            sem_post(&shm->sem_nodo_industrial);
            
            // Mostrar progreso cada 6 horas
            if (hora % 6 == 0) {
                printf("[Orquestador] Día %d, Hora %d: Simulación activa...\n", dia, hora);
            }

            // Esperar a que los nodos terminen la hora
            sem_wait(&shm->sem_nodo_residencial_listo_hora);
            sem_wait(&shm->sem_nodo_industrial_listo_hora);
            //sem_wait(&shm->sem_monitoreo);

            sem_post(&shm->sem_auditor);
            //sem_post(&shm->sem_monitoreo);

            //sem_wait(&shm->sem_auditor_terminado);
            //sem_wait(&shm->sem_monitoreo_terminado);
        }
        if (dia < DIAS_SIMULACION) {
            sem_post(&shm->sem_nodo_residencial_dia_fin);
            sem_post(&shm->sem_nodo_industrial_dia_fin);
        }

    }
    
    // Terminar simulación
    shm->simulacion_activa = false;
    printf("[Orquestador] Simulación completada. Enviando señales de terminación...\n");


    // Enviar SIGTERM a todos los procesos hijos
    for (int i = 0; i < 2; i++) { // Solo verificar el proceso residencial
        if (pids[i] > 0) {
            printf("[Orquestador] Enviando SIGTERM a PID %d\n", pids[i]);
            if (kill(pids[i], SIGTERM) < 0) {
                perror("[Orquestador] Error al enviar señal");
            }
        }
    }
    
    // Esperar a que todos los procesos terminen
    printf("[Orquestador] Esperando finalización de procesos...\n");
    for (int i = 0; i < 2; i++) { // Solo verificar el proceso residencial
        if (pids[i] > 0) {
            int status;
            pid_t result = waitpid(pids[i], &status, 0);
            if (result > 0) {
                if (WIFEXITED(status)) {
                    printf("[Orquestador] PID %d terminó con código %d\n", pids[i], WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("[Orquestador] PID %d terminó por señal %d\n", pids[i], WTERMSIG(status));
                }
            }
        }
    } 
    
    // Mostrar resultados finales
    mostrar_resultados(shm);
    
    // Limpieza
    printf("[Orquestador] Limpiando memoria compartida...\n");
    destruir_memoria_compartida(shm, shm_fd);
    
    printf("[Orquestador] Simulación finalizada.\n");
    return EXIT_SUCCESS;
}

// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================

static int crear_memoria_compartida(MemoriaCompartida **shm) {
    // Crear bloque de memoria compartida
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[Orquestador] shm_open");
        return -1;
    }
    
    // Establecer tamaño
    if (ftruncate(shm_fd, sizeof(MemoriaCompartida)) < 0) {
        perror("[Orquestador] ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return -1;
    }
    
    // Mapear a memoria
    *shm = mmap(NULL, sizeof(MemoriaCompartida), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (*shm == MAP_FAILED) {
        perror("[Orquestador] mmap");
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
    shm->tiempo_espera_total_micros = 0.0;
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
    sem_init(&shm->sem_auditor_terminado, 1, 0);
    sem_init(&shm->sem_monitoreo, 1, 0);

    // Inicializar semáforos de sincronización por hora (listos)
    sem_init(&shm->sem_nodo_residencial_listo_hora, 1, 0);
    sem_init(&shm->sem_nodo_industrial_listo_hora, 1, 0);
    
    // Inicializar semáforos adicionales para sincronización entre procesos
    sem_init(&shm->sem_nodo_residencial_dia_fin, 1, 0);
    sem_init(&shm->sem_nodo_industrial_dia_fin, 1, 0);
    sem_init(&shm->sem_residencial_listo, 1, 0);
    sem_init(&shm->sem_industrial_listo, 1, 0);
    sem_init(&shm->sem_residencial_escoge_maximo, 1, 0);
    sem_init(&shm->sem_nodos_libres, 1, NUM_NODOS);
    sem_init(&shm->sem_sync_residencial, 1, 0);
    sem_init(&shm->sem_sync_industrial, 1, 0);
    sem_init(&shm->activador_industrial, 1, 0);
    sem_init(&shm->activador_residencial, 1, 0);
    
    // Inicializar control de solicitudes
    shm->max_solicitudes_residencial = 0;
    
    // Inicializar microseconds y su semáforo
    shm->microseconds = 0;
    sem_init(&shm->microseconds_sem, 1, 1);
    
    // Inicializar rwlock global para nodos
    pthread_rwlockattr_t rwlock_attr;
    pthread_rwlockattr_init(&rwlock_attr);
    pthread_rwlockattr_setpshared(&rwlock_attr, PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(&shm->mutex_nodos, &rwlock_attr);
    pthread_rwlockattr_destroy(&rwlock_attr);
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
    
    // Destruir semáforos adicionales
    sem_destroy(&shm->sem_nodo_residencial_dia_fin);
    sem_destroy(&shm->sem_nodo_industrial_dia_fin);
    sem_destroy(&shm->sem_residencial_listo);
    sem_destroy(&shm->sem_industrial_listo);
    sem_destroy(&shm->sem_residencial_escoge_maximo);
    sem_destroy(&shm->sem_nodos_libres);
    sem_destroy(&shm->sem_nodo_residencial_listo_hora);
    sem_destroy(&shm->sem_nodo_industrial_listo_hora);
    sem_destroy(&shm->sem_sync_residencial);
    sem_destroy(&shm->sem_sync_industrial);
    
    // Destruir semáforo de microseconds
    sem_destroy(&shm->microseconds_sem);
    
    // Destruir rwlock global
    pthread_rwlock_destroy(&shm->mutex_nodos);

    // Destruir mutex
    pthread_mutex_destroy(&shm->mutex_metricas);
    
    // Desmapear
    if (munmap(shm, sizeof(MemoriaCompartida)) < 0) {
        perror("[Orquestador] munmap");
    }
    
    // Cerrar descriptor
    close(shm_fd);
    
    // Eliminar objeto de memoria compartida
    if (shm_unlink(SHM_NAME) < 0) {
        perror("[Orquestador] shm_unlink");
    }
}

static pid_t lanzar_proceso(const char *ejecutable) {
    // Check if process is already running by looking for it in /proc
    /*
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "pgrep -f %s", ejecutable);
    printf("%s\n", cmd);
    FILE *fp = popen(cmd, "r");
    if (fp != NULL) {
        char pid_str[32];
        if (fgets(pid_str, sizeof(pid_str), fp) != NULL) {
            printf("Process already running: %s", pid_str);
            // Process is already running
            pid_t existing_pid = atoi(pid_str);
            pclose(fp);
            printf("%d\n", existing_pid);
            
            printf("[Orquestador] Proceso %s ya está ejecutándose (PID: %d)\n", ejecutable, existing_pid);
            return existing_pid;
        
        } else {
            pclose(fp);
        }
    }*/
    
    // Process not running, launch it
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("[Orquestador] fork");
        return -1;
    } else if (pid == 0) {
        // Proceso hijo
        execl(ejecutable, ejecutable, NULL);
        
        // Si execl retorna, hubo un error
        fprintf(stderr, "[Orquestador] Error al ejecutar %s: %s\n", ejecutable, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Proceso padre
    printf("[Orquestador] Lanzado %s (PID: %d)\n", ejecutable, pid);
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
        double eficiencia = shm->tiempo_espera_total_micros / shm->total_consultas_realizadas;
        DT dt = micros_to_DT(eficiencia, shm->microseconds);
        printf("Tiempo promedio de espera: [H: %2d, M: %02d, S: %02d]\n", dt.horas, dt.minutos, dt.segundos);
    }
    
    printf("========================================\n");
}

static void manejador_senal(int sig) {
    (void)sig;
    simulacion_terminada = 1;
    printf("\n[Orquestador] Señal recibida. Terminando simulación...\n");
}