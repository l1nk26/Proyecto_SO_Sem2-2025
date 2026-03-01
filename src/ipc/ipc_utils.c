#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ipc_utils.h"

// Variable estatica para guardar el fd de shm (solo para uso interno)
static int shm_fd_global = -1;

MemoriaCompartida* conectar_shm(void) {
    // Abrir memoria compartida existente (sin O_CREAT)
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm_fd < 0) {
        perror("[IPC] Error al abrir memoria compartida (eco_flow_main esta corriendo?)");
        return NULL;
    }
    
    // Guardar fd para desconectar despues
    shm_fd_global = shm_fd;
    
    // Mapear a memoria
    MemoriaCompartida *shm = mmap(NULL, sizeof(MemoriaCompartida), 
                                   PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("[IPC] Error al mapear memoria compartida");
        close(shm_fd);
        shm_fd_global = -1;
        return NULL;
    }
    
    printf("[IPC] Conectado a memoria compartida '%s'\n", SHM_NAME);
    return shm;
}

void desconectar_shm(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    
    if (munmap(shm, sizeof(MemoriaCompartida)) < 0) {
        perror("[IPC] Error al desmapear memoria");
    }
    
    if (shm_fd_global >= 0) {
        close(shm_fd_global);
        shm_fd_global = -1;
    }
    
    printf("[IPC] Desconectado de memoria compartida\n");
}

// ============================================================================
// SINCRONIZACION POR HORA
// ============================================================================

void esperar_hora_nodo_industrial(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    sem_wait(&shm->sem_nodo_industrial);
}

void esperar_hora_nodo_residencial(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    sem_wait(&shm->sem_nodo_residencial);
}

void esperar_hora_auditor(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    sem_wait(&shm->sem_auditor);
}

void esperar_hora_monitoreo(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    sem_wait(&shm->sem_monitoreo);
}

// ============================================================================
// LOCKS DE NODOS (READER-WRITER)
// ============================================================================

// Intentar reservar nodo (modo escritura)
// Retorna 0 si exito, -1 si error
int reservar_nodo(MemoriaCompartida *shm, int id_nodo, int usuario_id) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return -1;
    
    // Primero esperar en el semáforo de nodos libres
    
    // Usar wrlock global para marcar el nodo como ocupado
    // pthread_rwlock_wrlock(&shm->mutex_nodos);
    shm->valvulas[id_nodo].ocupado = true;
    shm->valvulas[id_nodo].usuario_id = usuario_id;
    // pthread_rwlock_unlock(&shm->mutex_nodos);
    
    return 0;
}

// Liberar nodo reservado
void liberar_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return;
    
    // Usar wrlock global para marcar el nodo como libre
    //pthread_rwlock_wrlock(&shm->mutex_nodos);
    shm->valvulas[id_nodo].ocupado = false;
    //pthread_rwlock_unlock(&shm->mutex_nodos);
    
    sem_post(&shm->sem_nodos_libres);
}

// Adquirir lock de lectura global para acceder a nodos
// Retorna 0 si exito, -1 si error
int leer_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return -1;
    
    // Usar rdlock global - el llamador debe adquirirlo antes de leer cualquier nodo
    // Esta función ahora es un no-op porque el lock ya debe estar adquirido
    (void)shm;
    return 0;
}

// Liberar lock de lectura global
void terminar_lectura_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return;
    
    // No-op - el lock global se libera externamente
    (void)shm;
    (void)id_nodo;
}

// ============================================================================
// MUTEX DE METRICAS
// ============================================================================

void lock_metricas(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    pthread_mutex_lock(&shm->mutex_metricas);
}

void unlock_metricas(MemoriaCompartida *shm) {
    if (shm == NULL) return;
    pthread_mutex_unlock(&shm->mutex_metricas);
}

// ============================================================================
// FUNCIONES ADICIONALES PARA SINCRONIZACIÓN
// ============================================================================

void set_max_solicitudes_residencial(MemoriaCompartida *shm, int max_solicitudes) {
    if (shm == NULL) return;
    shm->max_solicitudes_residencial = max_solicitudes;
}