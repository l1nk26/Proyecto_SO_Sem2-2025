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
int reservar_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return -1;
    
    // Adquirir lock de escritura (bloquea lectores y escritores)
    int ret = pthread_rwlock_wrlock(&shm->valvulas[id_nodo].rwlock_nodo);
    if (ret != 0) {
        fprintf(stderr, "[IPC] Error al adquirir wrlock en nodo %d: %s\n", 
                id_nodo, strerror(ret));
        return -1;
    }
    
    // Marcar como ocupado
    shm->valvulas[id_nodo].ocupado = true;
    return 0;
}

// Liberar nodo reservado
void liberar_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return;
    
    // Marcar como libre antes de liberar el lock
    shm->valvulas[id_nodo].ocupado = false;
    
    pthread_rwlock_unlock(&shm->valvulas[id_nodo].rwlock_nodo);
}

// Adquirir lock de lectura en nodo (para lectura concurrente)
// Retorna 0 si exito, -1 si error
int leer_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return -1;
    
    int ret = pthread_rwlock_rdlock(&shm->valvulas[id_nodo].rwlock_nodo);
    if (ret != 0) {
        fprintf(stderr, "[IPC] Error al adquirir rdlock en nodo %d: %s\n", 
                id_nodo, strerror(ret));
        return -1;
    }
    return 0;
}

// Liberar lock de lectura
void terminar_lectura_nodo(MemoriaCompartida *shm, int id_nodo) {
    if (shm == NULL || id_nodo < 0 || id_nodo >= NUM_NODOS) return;
    pthread_rwlock_unlock(&shm->valvulas[id_nodo].rwlock_nodo);
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