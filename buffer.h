#ifndef BUFFER_H
#define BUFFER_H

#include "common.h"

// Inicializa el buffer circular: índices, contador y condvars/mutex
void buffer_init(TaskBuffer *tb);

// Agrega una tarea al buffer (operación de productor)
void buffer_push(TaskBuffer *tb, Task t);

// Extrae una tarea del buffer (operación de consumidor)
Task buffer_pop(TaskBuffer *tb);

#endif // BUFFER_H
