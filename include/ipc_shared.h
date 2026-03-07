#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include <stdint.h>

// Nombre del bloque de memoria compartida en /dev/shm/
#define SHM_NAME "/eco_flow_shm"

// --- CONSTANTES DE LA SIMULACIÓN ---
#define NUM_NODOS 10
#define DIAS_SIMULACION 30
#define HORA_INICIO 6
#define HORA_FIN 18

// 1. Estructura individual para cada Nodo de Flujo (Válvula)
typedef struct {
    int id_nodo;                   // ID del 0 al 9
    bool ocupado;                  // true = reservado, false = libre
    int usuario_id;                 // ID del usuario que reservó el nodo
    double consumo_horario;         // Consumo acumulado en la hora git actual
    
    // Candado Lectores/Escritores exclusivo para esta válvula
    pthread_rwlock_t rwlock_nodo;  
} NodoFlujo;

// 2. Estructura principal de la Memoria Compartida
typedef struct {
    // El arreglo de los 10 nodos
    NodoFlujo valvulas[NUM_NODOS];

    // --- RESULTADOS A CONTABILIZAR ---
    double total_metros_cubicos;
    int amonestaciones_digitales;
    int senales_criticas;
    int senales_estandar;
    
    // Variables para calcular eficiencia (Monitoreo)
    int64_t tiempo_espera_total_micros; // en microsegundos
    int total_consultas_realizadas;
    int total_nodos_encontrados_ocupados;

    // --- ESTADO DE LA SIMULACIÓN ---
    int dia_actual;                // Del 1 al 30
    int hora_actual;               // De 6 a 18
    int minuto_actual;
    bool simulacion_activa;        // Pasa a false cuando termina el mes

    // --- SINCRONIZACIÓN DE MÉTRICAS ---
    // Mutex para evitar race conditions al sumar metros cúbicos o amonestaciones
    pthread_mutex_t mutex_metricas; 

    // --- SINCRONIZACIÓN POR HORA SIMULADA ---
    // Semaforos para que cada proceso sepa cuando paso una hora
    sem_t sem_nodo_industrial;     // Para nodo_industrial
    sem_t sem_nodo_residencial;    // Para nodo_residencial  
    sem_t sem_auditor;             // Para auditor_de_flujo
    sem_t sem_monitoreo;           // Para monitoreo_de_presion
    sem_t sem_residencial_escoge_maximo; // Para que residencial escoja su maximo de solicitudes
    sem_t sem_sync_residencial;
    sem_t sem_sync_industrial;

    sem_t sem_nodo_residencial_dia_fin;        // Para indicar fin del día
    sem_t sem_nodo_industrial_dia_fin;        // Para indicar fin del día
    sem_t sem_residencial_listo;    // Para indicar que residencial está listo
    sem_t sem_industrial_listo;    // Para indicar que industrial está listo
    sem_t sem_nodos_libres;        // Para controlar acceso a nodos libres
    
    // --- SINCRONIZACIÓN POR HORA (LISTOS) ---
    sem_t sem_nodo_residencial_listo_hora;  // Residencial listo para la hora
    sem_t sem_nodo_industrial_listo_hora;   // Industrial listo para la hora
    
    // --- CONTROL DE SOLICITUDES ---
    int max_solicitudes_residencial; // Máximo de solicitudes residenciales por día

    int microseconds;
    sem_t microseconds_sem;

    
    // --- SINCRONIZACIÓN GLOBAL DE NODOS ---
    pthread_rwlock_t mutex_nodos;   // Lock global para acceso a nodos

    sem_t activador_industrial;
    sem_t activador_residencial;
    
    // --- SINCRONIZACIÓN DE AUDITOR ---
    sem_t sem_auditor_terminado;
    sem_t sem_auditor_listas;

} MemoriaCompartida;

#endif // IPC_SHARED_H