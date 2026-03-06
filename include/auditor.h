#ifndef AUDITOR_H
#define AUDITOR_H

#include <pthread.h>
#include "ipc_shared.h"


// Funciones principales
void inicializar_auditor(void);
void cleanup_auditor(void);

// Hilos del auditor
void* hilo_calculo_horario(void *arg);


#endif // AUDITOR_H