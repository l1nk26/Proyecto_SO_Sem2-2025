# Eco Flow Simulation

Sistema de simulación de flujo de agua con múltiples procesos coordinados mediante memoria compartida y semáforos POSIX.

## 📋 Descripción del Proyecto

Eco Flow Simulation es un sistema distribuido que simula el consumo de agua a través de 10 válvulas (nodos de flujo). El sistema coordina múltiples procesos que representan diferentes roles:

- **Líder/Orquestador** (`eco_flow_main`): Proceso principal que crea memoria compartida, lanza procesos hijos, sincroniza por hora simulada y recolecta resultados
- **Nodos Residenciales** (`residencial`): Genera carga de usuarios residenciales con consumo aleatorio
- **Nodos Industriales** (`industrial`): Genera carga de usuarios industriales con patrones de consumo predecibles
- **Auditor de Flujo** (`auditor_de_flujo`): Valida consumos >500L y emite amonestaciones
- **Monitoreo de Presión** (`monitoreo_de_presion`): Realiza lecturas concurrentes de estado del sistema

## 🏗️ Arquitectura

### Memoria Compartida
El sistema utiliza un bloque de memoria compartida (`/dev/shm/eco_flow_shm`) con la siguiente estructura:

```c
typedef struct {
    NodoFlujo valvulas[10];                    // 10 válvulas con locks individuales
    double total_metros_cubicos;               // Métrica global de consumo
    int amonestaciones_digitales;                // Amonestaciones emitidas
    int senales_criticas;                      // Señales críticas enviadas
    int senales_estandar;                     // Señales estándar enviadas
    double tiempo_espera_total_ms;              // Tiempo total de espera del sistema
    int total_consultas_realizadas;           // Consultas realizadas
    int total_nodos_encontrados_ocupados;      // Nodos encontrados ocupados
    int dia_actual;                           // Día actual de simulación (1-30)
    int hora_actual;                          // Hora actual (6-18)
    bool simulacion_activa;                   // Estado de la simulación
    pthread_mutex_t mutex_metricas;             // Mutex para métricas globales
    int microseconds;                          // Control de velocidad de simulación
    // ... semáforos para sincronización
} MemoriaCompartida;
```

### Sincronización
- **Semáforos POSIX**: Cada proceso tiene un semáforo específico para esperar señales de hora
- **Reader-Writer Locks**: Cada válvula tiene su propio `pthread_rwlock_t` para acceso concurrente
- **Mutex Global**: Para actualización segura de métricas

## 🚀 Compilación y Ejecución

### Prerrequisitos
```bash
# Sistema operativo: Linux (recomendado) o macOS
# Compilador: GCC con soporte para pthread
$ gcc --version
```

### Compilar el proyecto
```bash
# Compilar versión normal
make all

# Compilar versión con debugging
make debug

# Limpiar archivos compilados
make clean

# Ver ayuda
make help
```

### Ejecutar la simulación
```bash
# Ejecución normal (1 segundo por hora simulada)
./eco_flow

# Ejecución rápida (--fast mode, sin delays)
./eco_flow --fast
```

## 📁 Estructura del Proyecto

```
eco_flow_2025/
├── include/                    # Cabeceras públicas
│   ├── ipc_shared.h           # Estructuras de memoria compartida
│   ├── ipc_utils.h            # Funciones helper para IPC
│   ├── auditor.h
│   ├── nodos.h
│   └── monitoreo.h
├── src/
│   ├── main/                  # [Dev 1: Líder/Orquestador]
│   │   └── eco_flow_main.c    # Setup de IPC, fork(), recolección
│   ├── ipc/                   # [Dev 1: Utilerías IPC]
│   │   └── ipc_utils.c        # Envoltorios para shm_open, sem_open, etc.
│   ├── auditor/               # [Dev 2: Auditoría y Validaciones]
│   │   └── auditor.c          # Lógica del Auditor, validaciones >500L
│   ├── nodos/                 # [Dev 3: Generación de Carga]
│   │   ├── residencial.c      # Usuarios residenciales (aleatorio)
│   │   └── industrial.c       # Usuarios industriales (patrones)
│   └── monitoreo/             # [Dev 4: Lecturas y Reglas]
│       └── monitor.c          # Hilos de lectura concurrente
├── logs/                      # Archivos de log generados en ejecución
├── Makefile                   # Configuración de compilación
└── README.md                  # Este documento
```

## 🔧 APIs Clave del Sistema

### Memoria Compartida
- `shm_open()` - Crear/abrir objeto de memoria compartida
- `ftruncate()` - Establecer tamaño del bloque
- `mmap()` - Mapear a espacio de direcciones del proceso
- `munmap()` - Desmapear memoria
- `shm_unlink()` - Eliminar objeto del sistema

### Sincronización
- `pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)` - Configurar locks inter-proceso
- `pthread_rwlock_init()` - Inicializar reader-writer locks
- `sem_init(&sem, 1, 0)` - Inicializar semáforo compartido
- `sem_wait()` - Esperar señal (bloqueante)
- `sem_post()` - Enviar señal (desbloqueante)

### Procesos
- `fork()` - Crear proceso hijo
- `exec()` - Reemplazar imagen del proceso
- `waitpid()` - Esperar terminación de hijo
- `kill(pid, SIGTERM)` - Enviar señal de terminación

## 📊 Modos de Operación

### Modo Normal
```bash
./eco_flow
```
- 1 segundo = 1 hora simulada
- Permite observar el comportamiento en tiempo real
- Útil para debugging y presentación

### Modo Fast
```bash
./eco_flow --fast
```
- Sin delays entre horas simuladas
- `microseconds = 1000` (indicador de modo rápido)
- Ideal para pruebas automatizadas y CI/CD

## 🔄 Flujo de Ejecución

1. **Inicialización**:
   - `eco_flow_main` crea memoria compartida
   - Inicializa 10 válvulas con rwlocks
   - Configura semáforos inter-proceso
   - Establece `microseconds` según modo

2. **Lanzamiento**:
   - `fork()` + `exec()` para cada proceso hijo
   - Hijos se conectan a memoria existente
   - Cada proceso espera su semáforo específico

3. **Simulación** (30 días × 13 horas):
   - **Cada hora**: Main hace `sem_post()` ×4
   - **Procesos**: Hacen `sem_wait()` y procesan
   - **Sincronización**: Reader-writer locks por válvula
   - **Métricas**: Actualización con mutex global

4. **Terminación**:
   - Main pone `simulacion_activa = false`
   - Envía `SIGTERM` a todos los hijos
   - `waitpid()` para recolectar estados
   - Limpia memoria compartida

## 📝 Procesos y Responsabilidades

### 🎯 eco_flow_main (Líder)
**Responsabilidades**:
- Crear y configurar memoria compartida
- Lanzar procesos hijos
- Sincronizar por hora simulada
- Manejar señales (SIGINT, SIGTERM)
- Recolectar y mostrar resultados finales
- Limpiar recursos del sistema

**Archivos**: `src/main/eco_flow_main.c`

### 🏠 residencial (Nodos Residenciales)
**Responsabilidades**:
- Generar usuarios residenciales aleatorios
- Consumo entre 50-500L por solicitud
- Reservar/liberar válvulas usando rwlocks
- Esperar señales de hora con `esperar_hora_nodo_residencial()`

**Archivos**: `src/nodos/residencial.c` *(por implementar)*

### 🏭 industrial (Nodos Industriales)
**Responsabilidades**:
- Generar usuarios industriales con patrones
- Consumos predecibles (horas pico/valle)
- Gestión de válvulas con rwlocks
- Esperar señales de hora con `esperar_hora_nodo_industrial()`

**Archivos**: `src/nodos/industrial.c` *(por implementar)*

### 🔍 auditor_de_flujo (Auditor)
**Responsabilidades**:
- Monitorear consumos por válvula
- Validar consumos >500L
- Emitir amonestaciones digitales
- Actualizar métricas globales
- Esperar señales de hora con `esperar_hora_auditor()`

**Archivos**: `src/auditor/auditor.c` *(por implementar)*

### 📊 monitoreo_de_presion (Monitor)
**Responsabilidades**:
- Lecturas concurrentes del sistema
- Calcular eficiencia y tiempos de espera
- Contabilizar nodos ocupados/libres
- Actualizar estadísticas en tiempo real
- Esperar señales de hora con `esperar_hora_monitoreo()`

**Archivos**: `src/monitoreo/monitor.c` *(por implementar)*

## 🔌 Comunicación Entre Procesos

### Conexión a Memoria Compartida
```c
#include "ipc_utils.h"

int main() {
    MemoriaCompartida *shm = conectar_shm();
    if (!shm) {
        fprintf(stderr, "Error: No se pudo conectar a memoria compartida\n");
        return EXIT_FAILURE;
    }
    
    // Usar shm->valvulas[], semáforos, etc.
    
    desconectar_shm(shm);
    return EXIT_SUCCESS;
}
```

### Uso de Sincronización
```c
// Reservar válvula para escritura
if (reservar_nodo(shm, id_nodo, usuario_id) == 0) {
    // Usar válvula exclusivamente
    shm->valvulas[id_nodo].usuario_id = usuario_id;
    liberar_nodo(shm, id_nodo);
}

// Lectura concurrente
if (leer_nodo(shm, id_nodo) == 0) {
    bool ocupado = shm->valvulas[id_nodo].ocupado;
    terminar_lectura_nodo(shm, id_nodo);
}

// Actualizar métricas globalmente
lock_metricas(shm);
shm->total_metros_cubicos += consumo;
unlock_metricas(shm);
```

### Control de Velocidad
```c
// Leer configuración de velocidad
int us = leer_microseconds(shm);
if (us > 0) {
    printf("Modo FAST activado (%d μs)\n", us);
} else {
    printf("Modo normal activado\n");
}
```

## 🐛 Troubleshooting

### Problemas Comunes

#### Memoria Compartida no se crea
```bash
# Verificar permisos
ls -la /dev/shm/

# Limpiar memoria huérfana
sudo rm -f /dev/shm/eco_flow_shm
```

#### Procesos no se lanzan
```bash
# Verificar ejecutables
ls -la residencial industrial auditor_de_flujo monitoreo_de_presion

# Verificar dependencias
ldd residencial
```

#### Sincronización bloqueada
```bash
# Verificar semáforos
ipcs -s

# Debug con valgrind
valgrind --tool=helgrind ./eco_flow
```

### Logs y Depuración
```bash
# Logs generados en logs/
tail -f logs/simulacion_mes.log

# Compilar con símbolos de debug
make debug
gdb ./eco_flow
```

## 📈 Métricas y Resultados

### Indicadores Clave
- **Total metros cúbicos**: Consumo total de agua del sistema
- **Amonestaciones digitales**: Validaciones de consumo >500L
- **Señales críticas**: Alertas de alto consumo
- **Señales estándar**: Notificaciones normales
- **Tiempo promedio de espera**: Eficiencia del sistema
- **Nodos encontrados ocupados**: Tasa de utilización

### Fórmulas de Eficiencia
```
Eficiencia = Tiempo total de espera / Total consultas
Utilización = (Nodos ocupados / Total nodos) × 100
```

## 🔮 Extensiones Futuras

### Mejoras Planeadas
- [ ] Configuración vía archivo JSON/YAML
- [ ] Interfaz web para monitoreo en tiempo real
- [ ] Base de datos para persistencia histórica
- [ ] API REST para integración externa
- [ ] Modo distributed con múltiples máquinas
- [ ] Algoritmos de predicción de consumo

### Características Opcionales
- [ ] Modo verbose con logs detallados
- [ ] Configuración de número de válvulas
- [ ] Patrones de consumo personalizables
- [ ] Simulación de eventos extremos
- [ ] Exportación de resultados a CSV/JSON

## 👥 Contribución

### Desarrollo por Módulos
Cada desarrollador puede trabajar independientemente en su módulo:

1. **Dev 1 - Líder**: `src/main/`, `src/ipc/`
2. **Dev 2 - Auditor**: `src/auditor/`
3. **Dev 3 - Nodos**: `src/nodos/`
4. **Dev 4 - Monitoreo**: `src/monitoreo/`

### Flujo de Trabajo
1. Clonar repositorio: `git clone <repo>`
2. Crear rama: `git checkout -b feature/<modulo>`
3. Implementar funcionalidad
4. Probar localmente: `make debug && ./eco_flow`
5. Commitear cambios: `git add . && git commit -m "msg"`
6. Push y crear PR: `git push origin feature/<modulo>`

### Estilo de Código
- Usar funciones de `ipc_utils.h` para IPC
- Manejar todos los errores con `perror()` y `fprintf()`
- Liberar recursos en todos los caminos
- Documentar funciones complejas
- Seguir convención de nombres `[proceso]_accion()`

## 📄 Licencia

Este proyecto se desarrolla como parte del curso de Sistemas Operativos 2025.

---

**Última actualización**: Marzo 2025  
**Versión**: 1.0.0  
**Estado**: En desarrollo - Estructura base completada
