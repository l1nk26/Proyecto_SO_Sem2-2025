# Makefile for eco_flow_2026
# Compilador y configuración
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -pthread
DEBUG_FLAGS = -g -DDEBUG -fsanitize=address -fsanitize=thread
INCLUDES = -Iinclude

# Directorios
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests

# Archivos fuente
CORE_SOURCES = $(SRC_DIR)/core/logger.c $(SRC_DIR)/core/stats_collector.c
IPC_SOURCES = $(SRC_DIR)/ipc/sync_primitives.c $(SRC_DIR)/ipc/message_bus.c
MODULE_SOURCES = $(SRC_DIR)/modules/auditor.c $(SRC_DIR)/modules/nodo_residencial.c $(SRC_DIR)/modules/nodo_industrial.c $(SRC_DIR)/modules/monitoreo.c
MAIN_SOURCE = $(SRC_DIR)/main.c

# Todos los archivos fuente
ALL_SOURCES = $(CORE_SOURCES) $(IPC_SOURCES) $(MODULE_SOURCES) $(MAIN_SOURCE)

# Archivos objeto
CORE_OBJECTS = $(CORE_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
IPC_OBJECTS = $(IPC_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
MODULE_OBJECTS = $(MODULE_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
MAIN_OBJECT = $(MAIN_SOURCE:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

ALL_OBJECTS = $(CORE_OBJECTS) $(IPC_OBJECTS) $(MODULE_OBJECTS) $(MAIN_OBJECT)

# Ejecutable principal
TARGET = eco_flow_2026

# Tests
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TEST_DIR)/%.c=$(BUILD_DIR)/test_%.o)
TEST_TARGET = test_runner

# Reglas por defecto
.PHONY: all clean debug test run help

all: $(TARGET)

# Crear directorio de build
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/core $(BUILD_DIR)/ipc $(BUILD_DIR)/modules

# Compilar archivos objeto
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Enlazar ejecutable principal
$(TARGET): $(ALL_OBJECTS)
	$(CC) $(CFLAGS) $(ALL_OBJECTS) -o $@ -pthread -lm

# Compilación con debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)

# Tests
test: $(TEST_TARGET)

$(TEST_TARGET): $(CORE_OBJECTS) $(IPC_OBJECTS) $(MODULE_OBJECTS) $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ -pthread -lm
	./$(TEST_TARGET)

# Ejecutar el programa
run: $(TARGET)
	./$(TARGET)

# Limpiar archivos compilados
clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TEST_TARGET)

# Instalar dependencias (si es necesario)
install-deps:
	@echo "No se requieren dependencias externas para este proyecto"

# Analizar código con valgrind
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

# Generar documentación (si se usa doxygen)
docs:
	@echo "Generando documentación..."
	@if [ -f Doxyfile ]; then doxygen Doxyfile; else echo "No se encontró Doxyfile"; fi

# Mostrar ayuda
help:
	@echo "Uso: make [target]"
	@echo ""
	@echo "Targets disponibles:"
	@echo "  all          - Compilar el proyecto (default)"
	@echo "  debug        - Compilar con flags de debug"
	@echo "  test         - Compilar y ejecutar tests"
	@echo "  run          - Compilar y ejecutar el programa"
	@echo "  clean        - Limpiar archivos compilados"
	@echo "  install-deps - Instalar dependencias"
	@echo "  valgrind     - Ejecutar análisis con valgrind"
	@echo "  docs         - Generar documentación"
	@echo "  help         - Mostrar esta ayuda"

# Dependencias entre módulos
$(BUILD_DIR)/modules/auditor.o: $(BUILD_DIR)/core/logger.o $(BUILD_DIR)/ipc/message_bus.o
$(BUILD_DIR)/modules/nodo_residencial.o: $(BUILD_DIR)/core/logger.o $(BUILD_DIR)/ipc/sync_primitives.o
$(BUILD_DIR)/modules/nodo_industrial.o: $(BUILD_DIR)/core/logger.o $(BUILD_DIR)/ipc/sync_primitives.o
$(BUILD_DIR)/modules/monitoreo.o: $(BUILD_DIR)/core/stats_collector.o $(BUILD_DIR)/ipc/message_bus.o
$(BUILD_DIR)/main.o: $(CORE_OBJECTS) $(IPC_OBJECTS) $(MODULE_OBJECTS)
