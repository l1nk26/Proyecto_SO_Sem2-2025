#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "../ipc/ipc_utils.h"

// Constantes para el nodo industrial
#define MAX_SOLICITUDES_INDUSTRIAL 150
#define PROBABILIDAD_RESERVA 0.5
#define LIMITE_CONSUMO_CRITICO 500.0

// Variables globales
static volatile sig_atomic_t proceso_terminado = 0;
static MemoriaCompartida *shm = NULL;

// Prototipos
static void manejador_senal(int sig);
static int generar_numero_solicitudes(void);
static void procesar_solicitud(int id_solicitud);
static void esperar_asignacion(int id_solicitud);
static void consumir_agua(int id_solicitud);
static void cancelar_solicitud(int id_solicitud);
static void consultar_presion(int id_solicitud);
static void pagar_tarifa_excedente(int id_solicitud);
static double generar_consumo_litros(void);
static int obtener_nodo_disponible(void);

int main(void) {
    // Configurar manejador de señales
    struct sigaction sa;
    sa.sa_handler = manejador_senal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    // Inicializar semilla aleatoria
    srand(time(NULL) ^ getpid());
    
    // Conectar a memoria compartida
    shm = conectar_shm();
    if (shm == NULL) {
        fprintf(stderr, "[Industrial] Error: No se pudo conectar a memoria compartida\n");
        return EXIT_FAILURE;
    }
    
    printf("[Industrial] Proceso iniciado (PID: %d)\n", getpid());
    
    // Bucle principal de simulación
    while (shm->simulacion_activa && !proceso_terminado) {
        // Esperar señal de hora simulada
        esperar_hora_nodo_industrial(shm);
        
        if (!shm->simulacion_activa || proceso_terminado) break;
        
        printf("[Industrial] Día %d, Hora %d: Generando solicitudes...\n", 
               shm->dia_actual, shm->hora_actual);
        
        // Generar número aleatorio de solicitudes para esta hora
        int num_solicitudes = generar_numero_solicitudes();
        
        // Procesar cada solicitud
        for (int i = 0; i < num_solicitudes && !proceso_terminado; i++) {
            procesar_solicitud(i);
            
            // Pequeña pausa entre solicitudes para simular procesamiento
            usleep(10000); // 10ms
        }
        
        printf("[Industrial] Día %d, Hora %d: %d solicitudes procesadas\n", 
               shm->dia_actual, shm->hora_actual, num_solicitudes);
    }
    
    // Limpieza
    desconectar_shm(shm);
    printf("[Industrial] Proceso terminado\n");
    
    return EXIT_SUCCESS;
}

static void manejador_senal(int sig) {
    (void)sig;
    proceso_terminado = 1;
    printf("[Industrial] Señal recibida. Terminando proceso...\n");
}

static int generar_numero_solicitudes(void) {
    // Generar entre 0 y MAX_SOLICITUDES_INDUSTRIAL solicitudes
    // Pero asegurando que no exceda el límite total de 250
    int max_posible = MAX_SOLICITUDES_INDUSTRIAL;
    
    // Para simplificar, generamos un número aleatorio entre 0 y 30 por hora
    // Esto asegura que no superaremos las 250 solicitudes diarias en la mayoría de casos
    int solicitudes = rand() % 31; // 0-30 solicitudes por hora
    
    return solicitudes;
}

static void procesar_solicitud(int id_solicitud) {
    // Generar número aleatorio para determinar la acción
    double prob = (double)rand() / RAND_MAX;
    
    printf("[Industrial] Solicitud %d: ", id_solicitud);
    
    if (prob < PROBABILIDAD_RESERVA) {
        printf("Intentando reservar nodo\n");
        esperar_asignacion(id_solicitud);
    } else if (prob < 0.7) {
        printf("Consultando presión disponible\n");
        consultar_presion(id_solicitud);
    } else if (prob < 0.85) {
        printf("Cancelando solicitud\n");
        cancelar_solicitud(id_solicitud);
    } else if (prob < 0.95) {
        printf("Pagando tarifa de excedente\n");
        pagar_tarifa_excedente(id_solicitud);
    } else {
        printf("Consumiendo agua directamente\n");
        consumir_agua(id_solicitud);
    }
}

static void esperar_asignacion(int id_solicitud) {
    int nodo = obtener_nodo_disponible();
    
    if (nodo >= 0) {
        // Intentar reservar el nodo
        if (reservar_nodo(shm, nodo) == 0) {
            printf("[Industrial] Solicitud %d: Nodo %d reservado exitosamente\n", 
                   id_solicitud, nodo);
            
            // Simular tiempo de reserva (1 hora)
            sleep(1);
            
            // Consumir agua
            double consumo = generar_consumo_litros();
            
            // Actualizar métricas
            lock_metricas(shm);
            shm->total_metros_cubicos += consumo / 1000.0; // Convertir a m³
            
            // Determinar si es consumo crítico
            if (consumo > LIMITE_CONSUMO_CRITICO) {
                shm->senales_criticas++;
                printf("[Industrial] Solicitud %d: Consumo crítico de %.2f litros\n", 
                       id_solicitud, consumo);
            } else {
                shm->senales_estandar++;
            }
            unlock_metricas(shm);
            
            // Liberar el nodo
            liberar_nodo(shm, nodo);
            printf("[Industrial] Solicitud %d: Nodo %d liberado, consumo: %.2f litros\n", 
                   id_solicitud, nodo, consumo);
        } else {
            printf("[Industrial] Solicitud %d: Error al reservar nodo %d\n", 
                   id_solicitud, nodo);
        }
    } else {
        printf("[Industrial] Solicitud %d: No hay nodos disponibles\n", id_solicitud);
        
        // Actualizar métricas de eficiencia
        lock_metricas(shm);
        shm->total_nodos_encontrados_ocupados++;
        shm->tiempo_espera_total_ms += 1000.0; // 1 segundo de espera simulada
        unlock_metricas(shm);
    }
}

static void consumir_agua(int id_solicitud) {
    // Consumo directo sin reserva (emergencia)
    double consumo = generar_consumo_litros() * 1.5; // Mayor consumo en emergencia
    
    lock_metricas(shm);
    shm->total_metros_cubicos += consumo / 1000.0;
    shm->senales_criticas++; // Siempre se considera crítico el consumo directo
    unlock_metricas(shm);
    
    printf("[Industrial] Solicitud %d: Consumo de emergencia %.2f litros\n", 
           id_solicitud, consumo);
}

static void cancelar_solicitud(int id_solicitud) {
    // Verificar si tiene reservas activas (simplificado)
    int tiene_reserva = (rand() % 10 == 0); // 10% probabilidad de tener reserva
    
    if (!tiene_reserva) {
        // Emitir amonestación
        lock_metricas(shm);
        shm->amonestaciones_digitales++;
        unlock_metricas(shm);
        
        printf("[Industrial] Solicitud %d: Amonestación por cancelación sin reserva\n", 
               id_solicitud);
    } else {
        printf("[Industrial] Solicitud %d: Cancelación válida de reserva\n", id_solicitud);
    }
}

static void consultar_presion(int id_solicitud) {
    // Simular consulta de presión
    int nodos_disponibles = 0;
    
    for (int i = 0; i < NUM_NODOS; i++) {
        if (leer_nodo(shm, i) == 0) {
            if (!shm->valvulas[i].ocupado) {
                nodos_disponibles++;
            }
            terminar_lectura_nodo(shm, i);
        }
    }
    
    // Actualizar métricas
    lock_metricas(shm);
    shm->total_consultas_realizadas++;
    unlock_metricas(shm);
    
    printf("[Industrial] Solicitud %d: Presión disponible - %d/%d nodos libres\n", 
           id_solicitud, nodos_disponibles, NUM_NODOS);
}

static void pagar_tarifa_excedente(int id_solicitud) {
    // Simular pago de tarifa
    double tarifa = 50.0 + (rand() % 100); // $50-$150
    
    printf("[Industrial] Solicitud %d: Tarifa de excedente pagada: $%.2f\n", 
           id_solicitud, tarifa);
    
    // Liberar un nodo aleatorio si está ocupado (simulación)
    int nodo = rand() % NUM_NODOS;
    if (leer_nodo(shm, nodo) == 0) {
        if (shm->valvulas[nodo].ocupado) {
            terminar_lectura_nodo(shm, nodo);
            liberar_nodo(shm, nodo);
            printf("[Industrial] Solicitud %d: Nodo %d liberado por pago\n", 
                   id_solicitud, nodo);
        } else {
            terminar_lectura_nodo(shm, nodo);
        }
    }
}

static double generar_consumo_litros(void) {
    // Generar consumo entre 100 y 800 litros
    return 100.0 + (rand() % 701);
}

static int obtener_nodo_disponible(void) {
    // Buscar un nodo disponible
    for (int i = 0; i < NUM_NODOS; i++) {
        if (leer_nodo(shm, i) == 0) {
            if (!shm->valvulas[i].ocupado) {
                terminar_lectura_nodo(shm, i);
                return i;
            }
            terminar_lectura_nodo(shm, i);
        }
    }
    return -1; // No hay nodos disponibles
}