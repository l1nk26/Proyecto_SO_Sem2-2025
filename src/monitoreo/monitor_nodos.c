#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>
#include <math.h>

#include "../../include/ipc_utils.h"

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define BOLD "\033[1m"
#define DIM "\033[2m"
#define REVERSE "\033[7m"

static volatile bool running = true;
static MemoriaCompartida *shm = NULL;
static int update_interval_ms = 50; 

void limpiar_pantalla(bool clear_all) {
    printf("\033[?25l"); // Ocultar cursor siempre
    
    if (clear_all) {
        printf("\033[2J");  // Limpiar toda la pantalla (solo se usa una vez)
    }
    printf("\033[H");       // Mover cursor a la posición (0,0) [Arriba a la izquierda]
    fflush(stdout);
}
void mostrar_cursor() {
    fflush(stdout);
    printf("\033[?25h"); // Mostrar cursor
    fflush(stdout);
}

void mover_cursor(int fila, int columna) {
    printf("\033[%d;%dH", fila, columna);
}

void imprimir_header() {
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf(CYAN BOLD "╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║" WHITE "   SISTEMA DE MONITOREO DE NODOS - ECO FLOW SIMULATION                        " CYAN "║\n");
    printf("║" WHITE "   %s                                                        " CYAN "║\n", timestamp);
    printf("╚══════════════════════════════════════════════════════════════════════════════╝\n" RESET);
    printf("\n");
    fflush(stdout);
}

void imprimir_nodo_compacto(int id) {
    // Imprimir 5 nodos en una fila: id, id+1, id+2, id+3, id+4
    NodoFlujo *valvulas[5];
    const char *estados[5];
    const char *colores[5];
    
    // Obtener datos de los 5 nodos
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        valvulas[i] = &shm->valvulas[id + i];
        colores[i] = valvulas[i]->ocupado ? GREEN : RED;
        estados[i] = valvulas[i]->ocupado ? "OCUPADO" : "LIBRE";
    }
    
    // Primera línea: Headers (ancho fijo de 12 caracteres por caja)
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        printf("┌──────────┐ ");
    }
    printf("\n");
    
    // Segunda línea: Números de nodo (centrados)
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        printf("│" BOLD "   N%02d    " RESET "│ ", id + i);
    }

    printf("\n");
    
    // Tercera línea: Separadores
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        printf("├──────────┤ ");
    }
    printf("\n");
    
    // Cuarta línea: Estados (centrados)
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        printf("│" BOLD "%s%-10s" RESET "│ ", colores[i], estados[i]);
    }
    printf("\n");
    
    // Quinta línea: Usuarios (centrados)
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        printf("│" BOLD "  %06d  " RESET "│ ", valvulas[i]->usuario_id);
    }
    printf("\n");
    
    // Sexta línea: Cierre
    for (int i = 0; i < 5 && (id + i) < NUM_NODOS; i++) {
        printf("└──────────┘ ");
    }
    printf("\n");
}

void imprimir_nodos() {
    printf(BOLD "ESTADO DE LOS NODOS:\n\n" RESET);
    
    // Imprimir nodos en grupos de 5 por fila
    for (int i = 0; i < NUM_NODOS; i += 5) {
        imprimir_nodo_compacto(i);
    }
    fflush(stdout);
}

void imprimir_semaforo_y_estadisticas() {
    int sem_value = 0;
    sem_getvalue(&shm->sem_nodos_libres, &sem_value);
    
    int ocupados = 0;
    for (int i = 0; i < NUM_NODOS; i++) {
        if (shm->valvulas[i].ocupado) ocupados++;
    }
    
    float porc_libres = (float)sem_value * 100 / NUM_NODOS;
    float porc_ocupados = (float)ocupados * 100 / NUM_NODOS;
    double t_espera = shm->total_consultas_realizadas > 0 ? 
                      shm->tiempo_espera_total_ms / shm->total_consultas_realizadas : 0.0;

    // --- CAJA 1: SEMÁFORO ---
    printf(BOLD CYAN "╔═════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║" WHITE " %-75s " CYAN " ║\n", "SEMÁFORO DE NODOS LIBRES");
    printf("╠═════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║" WHITE " Disponibles: " BOLD GREEN "%02d" RESET WHITE " / " BOLD "%02d" RESET WHITE "(%5.1f%%) %-43s" CYAN "    ║\n", 
           sem_value, NUM_NODOS, porc_libres, "");
    
    // Barra visual ajustada a 50 caracteres de ancho interno
    printf("║" WHITE " [");
    for (int i = 0; i < NUM_NODOS; i++) {
        if (i < sem_value) printf(GREEN "█" RESET);
        else printf(RED "░" RESET);
    }
    // Relleno de espacios para mantener el borde derecho fijo (ajustar según NUM_NODOS)
    int padding_barra = 58 - NUM_NODOS; 
    printf("] " BOLD "%02d/%02d" RESET WHITE " libres %*s" CYAN "  ║\n", sem_value, NUM_NODOS, padding_barra, "");
    printf("╚═════════════════════════════════════════════════════════════════════════════╝\n" RESET);
    
    // --- CAJA 2: ESTADÍSTICAS GLOBALES ---
    printf(BOLD BLUE "╔═════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║" WHITE " %-75s " BLUE " ║\n", "ESTADÍSTICAS GLOBALES");
    printf("╠═════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║" WHITE " Ocupados:   " BOLD "%02d/%02d" RESET WHITE " (%5.1f%%) %-47s " BLUE " ║\n", ocupados, NUM_NODOS, porc_ocupados, "");
    printf("║" WHITE " Total m³:   " BOLD GREEN "%-10.2f" RESET WHITE " %-51s " BLUE " ║\n", shm->total_metros_cubicos, "");
    printf("║" WHITE " Tiempo esp: " BOLD YELLOW "%-10.1f ms" RESET WHITE " %-48s " BLUE " ║\n", t_espera, "");
    printf("╚═════════════════════════════════════════════════════════════════════════════╝\n" RESET);
    fflush(stdout);
}

void imprimir_info_sistema() {
    printf(BOLD MAGENTA "╔═════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║" WHITE " %-75s " MAGENTA " ║\n", "INFORMACIÓN DEL SISTEMA");
    printf("╠═════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║" WHITE " Hora Actual:              " BOLD "%02d:00" RESET WHITE " %-41s " MAGENTA "  ║\n", shm->hora_actual, "");
    printf("║" WHITE " Día Actual:               " BOLD "%-2d" RESET WHITE "    %-41s " MAGENTA "  ║\n", shm->dia_actual, "");
    printf("║" WHITE " Total Metros Cúbicos:    " BOLD GREEN "%-10.2f" RESET WHITE " %-38s " MAGENTA " ║\n", shm->total_metros_cubicos, "");
    printf("║" WHITE " Amonestaciones Digitales: " BOLD RED "%-10d" RESET WHITE " %-38s " MAGENTA "║\n", shm->amonestaciones_digitales, "");
    printf("║" WHITE " Señales Críticas:        " BOLD YELLOW "%-10d" RESET WHITE " %-38s " MAGENTA " ║\n", shm->senales_criticas, "");
    printf("║" WHITE " Señales Estándar:        " BOLD CYAN "%-10d" RESET WHITE " %-38s " MAGENTA " ║\n", shm->senales_estandar, "");
    printf("║" WHITE " Total Consultas:         " BOLD "%-10d" RESET WHITE " %-38s " MAGENTA " ║\n", shm->total_consultas_realizadas, "");
    printf("║" WHITE " Simulación Activa:       " BOLD "%-10s" RESET WHITE " %-38s " MAGENTA "  ║\n", shm->simulacion_activa ? "SÍ" : "NO", "");
    printf("╚═════════════════════════════════════════════════════════════════════════════╝\n" RESET);
    fflush(stdout);
}

void imprimir_controles() {
    printf(DIM "╔═════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║" WHITE " %-75s " DIM "║\n", "CONTROLES:");
    printf("║" WHITE " [Ctrl+C] - Salir del monitor %-45s  " DIM "║\n", "");
    printf("╠═════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║" WHITE " Velocidad: %-5d ms (%4.1f FPS) %-43s  " DIM "║\n", update_interval_ms, 1000.0/update_interval_ms, "");
    printf("╚═════════════════════════════════════════════════════════════════════════════╝\n" RESET);
    fflush(stdout);
}

void handle_signal(int sig) {
    running = false;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf(CYAN BOLD "Iniciando Monitor de Nodos...\n" RESET);
    
    printf(CYAN "🔍 Buscando sistema Eco Flow...\n" RESET);
    
    // Esperar a que el sistema esté disponible
    int intentos = 0;
    while (running && intentos < 60) { // Máximo 60 segundos esperando
        limpiar_pantalla(true);
        
        printf(DIM "   %d/60 - Esperando...\r" RESET, intentos);
        fflush(stdout);
        
        for (int i = 0; i < 10 && running; i++) {
            shm = conectar_shm();
            if (shm != NULL) {
                printf(GREEN "✓ Conectado a memoria compartida exitosamente\n" RESET);
                break;
            }
            
            usleep(100000); // 100ms
        }
        intentos++;
        if (intentos == 1) {
            printf(YELLOW " Esperando que el sistema Eco Flow inicie...\n" RESET);
            printf("   Inicie el sistema con: cd ../../ && make && ./eco_flow --normal\n");
        }
        if (shm != NULL) {
            printf(GREEN "✓ Conectado a memoria compartida exitosamente\n" RESET);
            break;
        }
    }
    
    if (shm == NULL) {
        printf(RED "\n Error: No se pudo conectar después de 60 segundos\n" RESET);
        printf("   Asegúrese de que el sistema está en ejecución.\n");
        return EXIT_FAILURE;
    }
    
/*     printf(GREEN "✓ Sistema detectado y sincronizado\n" RESET);
    printf("🚀 Iniciando monitoreo en tiempo real...\n");
    printf("   Presione Ctrl+C para salir\n");
    printf("   Velocidad: %dms (%.1f FPS)\n", update_interval_ms, 1000.0 / update_interval_ms);
    sleep(2); */
    
    limpiar_pantalla(true);

    while (running) {
        limpiar_pantalla(false);
        
        imprimir_header();
        
        // Mostrar nodos en formato compacto de 5 por fila
        imprimir_nodos();
        printf("\n");
        
        // Mostrar semáforo y estadísticas combinados
        imprimir_semaforo_y_estadisticas();
        printf("\n");
        
        imprimir_info_sistema();
        imprimir_controles();
        
        // Esperar el tiempo configurado
        usleep(update_interval_ms * 1000);
    }
    
    limpiar_pantalla(true);

    mostrar_cursor();
    printf(CYAN "Monitor de Nodos finalizado.\n" RESET);
    
    desconectar_shm(shm);
    return EXIT_SUCCESS;
}
