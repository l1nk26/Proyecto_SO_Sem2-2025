#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "monitoreo.h"
#include "ipc_utils.h"

void ejecutar_monitoreo() {
    // Conexión a la memoria compartida
    MemoriaCompartida *shm = conectar_shm();
    if (!shm) {
        fprintf(stderr, "[Monitor] Error de conexión a SHM\n");
        exit(EXIT_FAILURE);
    }

    printf("[Monitor] Vigilancia Eco-Flow 2026 activada.\n");

    while (shm->simulacion_activa) {
        // Espera señal del Orquestador al final de cada hora
        sem_wait(&shm->sem_monitoreo);

        if (!shm->simulacion_activa) break;

        realizar_lectura_presion(shm);
    }

    desconectar_shm(shm);
    printf("[Monitor] Proceso finalizado.\n");
}

void realizar_lectura_presion(MemoriaCompartida *shm) {
    int nodos_ocupados = 0;

    // Regla 2: Lectores/Escritores - Acceso simultáneo 
    for (int i = 0; i < 10; i++) {
        pthread_rwlock_rdlock(&shm->valvulas[i].rwlock_nodo);
        if (shm->valvulas[i].ocupado) {
            nodos_ocupados++;
        }
        pthread_rwlock_unlock(&shm->valvulas[i].rwlock_nodo);
    }

    // Actualización segura de métricas con Mutex Global
    pthread_mutex_lock(&shm->mutex_metricas);
    shm->total_consultas_realizadas++;
    shm->total_nodos_encontrados_ocupados += nodos_ocupados;
    pthread_mutex_unlock(&shm->mutex_metricas);

    printf("[Monitor] Hora %02d:00 - %d/10 válvulas activas.\n", shm->hora_actual, nodos_ocupados);
}

int main() {
    ejecutar_monitoreo();
    return 0;
}