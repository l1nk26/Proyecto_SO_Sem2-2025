#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include "ipc_shared.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// Conectar a memoria compartida existente (creada por eco_flow_main)
// Retorna puntero a memoria compartida o NULL si error
// El fd se almacena internamente para desconectar despues
MemoriaCompartida* conectar_shm(void);

// Desconectar de memoria compartida
void desconectar_shm(MemoriaCompartida *shm);

// Esperar señal de hora simulada (sem_wait en el semaforo correspondiente)
// Cada proceso usa su semaforo especifico
void esperar_hora_nodo_industrial(MemoriaCompartida *shm);
void esperar_hora_nodo_residencial(MemoriaCompartida *shm);
void esperar_hora_auditor(MemoriaCompartida *shm);
void esperar_hora_monitoreo(MemoriaCompartida *shm);

// Helpers para locks de nodos (lectura/escritura)
int reservar_nodo(MemoriaCompartida *shm, int id_nodo, int usuario_id);      // lock write
void liberar_nodo(MemoriaCompartida *shm, int id_nodo);       // unlock write
int leer_nodo(MemoriaCompartida *shm, int id_nodo);          // lock read
void terminar_lectura_nodo(MemoriaCompartida *shm, int id_nodo); // unlock read

// Helper para mutex de metricas
void lock_metricas(MemoriaCompartida *shm);
void unlock_metricas(MemoriaCompartida *shm);

// --- FUNCIONES ADICIONALES PARA SINCRONIZACIÓN ---
void set_max_solicitudes_residencial(MemoriaCompartida *shm, int max_solicitudes);

#endif // IPC_UTILS_H
