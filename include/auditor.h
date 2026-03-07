#ifndef AUDITOR_H
#define AUDITOR_H

#include <pthread.h>
#include "ipc_shared.h"


// Estructura del mensaje para alertas de consumo
typedef struct {
    int nodo_id;
    double litros_consumidos;
    int tipo_proceso;
    pid_t pid_proceso;
} MensajeAlerta;

// Funciones principales
void inicializar_auditor(void);
void cleanup_auditor(void);

// Hilos del auditor
void* hilo_procesar_alertas(void *arg);
void* hilo_calculo_horario(void *arg);

// Funciones auxiliares
void procesar_alerta_critica(const MensajeAlerta *msg);
void calcular_consumo_total_horario();

#endif // AUDITOR_H