# Eco Flow Simulation



## Monitor de Nodos - Eco Flow Simulation

Programa de monitoreo en tiempo real para visualizar el estado de los nodos del sistema Eco Flow.

## Características

- **Visualización en tiempo real** del estado de todos los nodos
- **Interfaz atractiva** con colores y barras de progreso
- **Estadísticas globales** del sistema
- **Información detallada** de cada nodo:
  - Estado (Ocupado/Libre)
  - ID de usuario asignado
  - Flujo actual con barra de progreso visual
  - Presión actual

## 🚀 Ejecución Rápida 

### Opción 1: Compilar y Ejecutar
```bash
cd src/monitor
chmod +x build_monitor.sh run_monitor.sh
./build_monitor.sh
```
Este script:
- 🔨 Compila el monitor automáticamente
- ⏳ Espera hasta 60 segundos por el sistema Eco Flow
- � Se sincroniza automáticamente cuando el sistema inicie
- 🚀 Inicia la visualización inmediatamente


### 🎯 **Flujo de Trabajo Ideal:**

1. **Terminal 1**: Iniciar el monitor primero
   ```bash
   cd src/monitor && ./build_monitor.sh
   ```
2. **Terminal 2**: Iniciar el sistema
   ```bash
   cd Proyecto_SO_Sem2-2025 && make && ./eco_flow -ms 100000
   ```
3. El monitor detectará automáticamente el sistema y comenzará a mostrar datos

## 📋 Requisitos Previos

**Opcional**: El sistema Eco Flow puede iniciarse antes o después del monitor. El monitor esperará automáticamente:

```bash
# Puede ejecutarse en cualquier orden
./monitor_nodos          # Primero (esperará hasta 60s)
./eco_flow -ms 100000    # Después (o antes)
```

## 🎮 Controles

- `Ctrl+C` - Salir del monitor
- El monitor se actualiza automáticamente

## 🎨 Visualización

El monitor muestra:
- **Grid de nodos**
- **Estadísticas globales**
- **Información del sistema**
- **Colores intuitivos**

## 🔧 Compilación Manual (si es necesario)

```bash
cd src/monitor
gcc -o monitor_nodos monitor_nodos.c \
    ../ipc/ipc_utils.c \
    ../funciones_auxiliares.c \
    -I../../include \
    -lpthread -lrt -lm
```
