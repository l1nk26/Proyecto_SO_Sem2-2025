#pragma once
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>

enum operacion {NINGUNA = 0, RESERVACION, CANCELACION, CONSULTA_PRESION};
enum estados_de_solicitud {PENDIENTE = 0, PROCESADA, CANCELADA, APLAZADA, DESCONOCIDO};


typedef struct {
    unsigned int usuario_id; // iniciado en 0 | Usuario referenciado
    unsigned int hilo_id; // iniciado en 0 | Indice de hilo
    struct timespec tiempo_espera_inicial;
    struct timespec tiempo_espera_final;
    double m3_consumidos;
    unsigned int edo_solicitud;
    unsigned int operacion;
} InfoHilo;

// Función para obtener timestamp en ms desde el inicio del proceso
static long obtener_timestamp_ms(void) {
    static struct timespec inicio;
    static int inicializado = 0;
    struct timespec ahora;
    
    if (!inicializado) {
        clock_gettime(CLOCK_MONOTONIC, &inicio);
        inicializado = 1;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    long segundos = ahora.tv_sec - inicio.tv_sec;
    long nanosegundos = ahora.tv_nsec - inicio.tv_nsec;
    
    if (nanosegundos < 0) {
        segundos -= 1;
        nanosegundos += 1000000000L;
    }
    
    return segundos * 1000 + nanosegundos / 1000000;
}

// Implementacion
static double factorial(int n) {
    if (n <= 1) return 1;
    double result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

static int poisson_ppf(double lambda, double p) {
    double L = exp(-lambda);
    int x = 0;
    double acum = 0;
    
    while (acum < p) {
        acum += L * pow(lambda, x) / factorial(x);
        x++;
    }
    
    return x - 1;
}

double calcular_tiempo_transcurrido(const struct timespec *inicio, const struct timespec *fin) {
    long segundos = fin->tv_sec - inicio->tv_sec;
    long nanosegundos = fin->tv_nsec - inicio->tv_nsec;

    if (nanosegundos < 0) {
        segundos -= 1;
        nanosegundos += 1000000000L;
    }
    return (double)segundos + (double)nanosegundos / 1000000000.0;
}

// Genera un número aleatorio en el rango [min, max]
int generar_random_range_int(int min, int max, unsigned int *seed) {
    return min + (rand_r(seed) % (max - min + 1));
}

// Genera un número aleatorio en el rango [min, max]
double generar_random_range(double min, double max, unsigned int *seed) {
    return min + (rand_r(seed) / (double)(RAND_MAX)) * (max - min);
}

void mostrar_estado_detalles_hilo(InfoHilo *info, const char *mensaje, const char *proceso) {
    printf("[%s] (%06ld) %s Hilo %d. Estado: operacion=%d, estado de solicitud=%d\n", 
           proceso, obtener_timestamp_ms(), mensaje, info->usuario_id, info->operacion, info->edo_solicitud);
}

void set_operacion(InfoHilo *info, int operacion) {
    info->operacion = operacion;
}

void set_edo_solicitud(InfoHilo *info, int edo_solicitud) {
    info->edo_solicitud = edo_solicitud;
}

int get_operacion(InfoHilo *info) {
    return info->operacion;
}

int get_edo_solicitud(InfoHilo *info) {
    return info->edo_solicitud;
}

