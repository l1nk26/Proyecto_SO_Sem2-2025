#pragma once
#include <time.h>
#include <math.h>

// Definiciones
static double factorial(int n);

static int poisson_ppf(double lambda, double p);

static double exponential_ppf(double lambda, double p);

static int maximo(int *v, int tam);

double calcular_tiempo_transcurrido(const struct timespec *inicio, const struct timespec *fin)

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

static double exponential_ppf(double lambda, double p) {
    return (-log(1 - p) / lambda);
}

static int maximo(int *v, int tam) {
    int max = 0;
    for (int i = 0; i < tam; i++) {
        if (max < v[i]) {
            max = v[i];
        }
    }
    return max;
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