#pragma once
#include <time.h>
#include <math.h>

// Definiciones
static double factorial(int n);

static int poisson_ppf(double lambda, double p);

double calcular_tiempo_transcurrido(const struct timespec *inicio, const struct timespec *fin);

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