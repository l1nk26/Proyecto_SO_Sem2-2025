#ifndef MONITOREO_H
#define MONITOREO_H

#include "ipc_shared.h"

// Función principal del módulo de monitoreo
void ejecutar_monitoreo();

// Lógica de escaneo de válvulas
void realizar_lectura_presion(MemoriaCompartida *shm);

#endif