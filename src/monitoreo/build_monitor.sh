#!/bin/bash

# Script de desarrollo para compilar y ejecutar el monitor de nodos
echo "-> Monitor de Nodos - Modo Desarrollo"
echo "====================================="

# Compilar
echo "-> Compilando monitor..."
gcc -o monitor_nodos monitor_nodos.c \
    ../ipc/ipc_utils.c \
    -I../../include \
    -lpthread \
    -lrt \
    -lm

if [ $? -ne 0 ]; then
    echo " Error en la compilación"
    exit 1
fi

echo " Monitor compilado exitosamente"
echo ""
echo "-> Iniciando monitor en modo desarrollo..."
echo "   El monitor esperará automáticamente el sistema Eco Flow"
echo "   Presione Ctrl+C para salir"
echo ""
echo "-> Puedes iniciar el sistema Eco Flow en otra terminal:"
echo "   cd ../../ && make && ./eco_flow --normal"
echo ""

# Ejecutar
./monitor_nodos

echo ""
echo "👋 Monitor finalizado"
