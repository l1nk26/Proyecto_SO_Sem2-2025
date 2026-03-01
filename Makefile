# Makefile for Eco Flow Simulation
# Compilador y configuración
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -pthread -D_GNU_SOURCE
DEBUG_FLAGS = -g -DDEBUG -fsanitize=address -fsanitize=thread
INCLUDES = -Iinclude

# Directorios
SRC_DIR = src
BUILD_DIR = build
LOGS_DIR = logs

# Archivos fuente por desarrollador
MAIN_SOURCES = $(SRC_DIR)/main/eco_flow_main.c
IPC_SOURCES = $(SRC_DIR)/ipc/ipc_utils.c
AUDITOR_SOURCES = $(SRC_DIR)/auditor/auditor.c
NODOS_SOURCES = $(SRC_DIR)/nodos/residencial.c $(SRC_DIR)/nodos/industrial.c
MONITOREO_SOURCES = $(SRC_DIR)/monitoreo/monitor.c

# Para prueba preliminar: solo compilar main + ipc
PRUEBA_SOURCES = $(MAIN_SOURCES) $(IPC_SOURCES)

# Todos los archivos fuente
ALL_SOURCES = $(PRUEBA_SOURCES) $(AUDITOR_SOURCES) $(NODOS_SOURCES) $(MONITOREO_SOURCES) $(NODOS_SOURCES)

# Archivos objeto
MAIN_OBJECTS = $(MAIN_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
IPC_OBJECTS = $(IPC_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
AUDITOR_OBJECTS = $(AUDITOR_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
NODOS_OBJECTS = $(NODOS_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
MONITOREO_OBJECTS = $(MONITOREO_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Archivos objeto (solo para prueba preliminar)
PRUEBA_OBJECTS = $(PRUEBA_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

ALL_OBJECTS = $(PRUEBA_OBJECTS)

# Ejecutable principal
TARGET = eco_flow

# Reglas por defecto
.PHONY: all clean clean-all debug test run help setup residencial industrial auditor monitor clean_cache clean_debug

# Compilar todos los ejecutables (principal + procesos)
all: setup $(TARGET) residencial industrial

# Crear directorios necesarios
setup:
	@mkdir -p $(BUILD_DIR) $(BUILD_DIR)/main $(BUILD_DIR)/ipc $(BUILD_DIR)/auditor $(BUILD_DIR)/nodos $(BUILD_DIR)/monitoreo $(LOGS_DIR)

# Compilar archivos objeto
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | setup
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Enlazar ejecutable principal
$(TARGET): $(ALL_OBJECTS)
	$(CC) $(CFLAGS) $(ALL_OBJECTS) -o $@ -pthread -lrt -lm

# Compilar procesos individuales
residencial: setup $(SRC_DIR)/nodos/residencial.c $(IPC_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC_DIR)/nodos/residencial.c $(IPC_OBJECTS) -o $@ -pthread -lrt -lm

industrial: setup $(SRC_DIR)/nodos/industrial.c $(IPC_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC_DIR)/nodos/industrial.c $(IPC_OBJECTS) -o $@ -pthread -lrt -lm

auditor: setup $(SRC_DIR)/auditor/auditor.c $(IPC_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC_DIR)/auditor/auditor.c $(IPC_OBJECTS) -o $@ -pthread -lrt -lm

monitor: setup $(SRC_DIR)/monitoreo/monitor.c $(IPC_OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC_DIR)/monitoreo/monitor.c $(IPC_OBJECTS) -o $@ -pthread -lrt -lm

# Compilación con debug
debug: 
	$(MAKE) CFLAGS="$(CFLAGS) $(DEBUG_FLAGS)"

clean_debug: 
	$(MAKE) clean

clean_cache:
	sudo rm /usr/local/bin/residencial

# Limpiar archivos compilados
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Limpiar todo incluyendo logs y ejecutables individuales
clean-all: clean
	rm -rf $(LOGS_DIR)
	rm -f residencial industrial auditor monitor

# Mostrar ayuda
help:
	@echo "Uso: make [target]"
	@echo ""
	@echo "Targets disponibles:"
	@echo "  all          - Compilar el proyecto (default)"
	@echo "  debug        - Compilar con flags de debug"
	@echo "  run          - Compilar y ejecutar el programa"
	@echo "  clean        - Limpiar archivos compilados"
	@echo "  clean-all    - Limpiar todo incluyendo logs"
	@echo "  setup        - Crear estructura de directorios"
	@echo "  help         - Mostrar esta ayuda"

# Dependencias entre módulos
$(BUILD_DIR)/main/eco_flow_main.o: $(BUILD_DIR)/ipc/ipc_utils.o
$(BUILD_DIR)/auditor/auditor.o: $(BUILD_DIR)/ipc/ipc_utils.o $(BUILD_DIR)/main/eco_flow_main.o
$(BUILD_DIR)/nodos/residencial.o: $(BUILD_DIR)/ipc/ipc_utils.o
$(BUILD_DIR)/nodos/industrial.o: $(BUILD_DIR)/ipc/ipc_utils.o
$(BUILD_DIR)/monitoreo/monitor.o: $(BUILD_DIR)/ipc/ipc_utils.o $(BUILD_DIR)/main/eco_flow_main.o
