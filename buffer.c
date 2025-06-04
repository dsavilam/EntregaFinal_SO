#include <stdlib.h>
#include "buffer.h"

void buffer_init(TaskBuffer *tb) {
    tb->in = 0;
    tb->out = 0;
    tb->count = 0;
    // Inicializa el mutex en estado desbloqueado
    pthread_mutex_init(&tb->mux, NULL);
    // Inicializa las variables de condición
    pthread_cond_init(&tb->not_empty, NULL);
    pthread_cond_init(&tb->not_full, NULL);
}

// Función para el productor: inserta una tarea en el buffer
void buffer_push(TaskBuffer *tb, Task t) {
    // Bloquea el mutex antes de acceder al buffer
    pthread_mutex_lock(&tb->mux);
    // Si el buffer está lleno, espera hasta que haya espacio (condición not_full)
    while (tb->count == MAX_TASK_BUFFER) {
        pthread_cond_wait(&tb->not_full, &tb->mux);
    }
    // Inserta la tarea en la posición "in"
    tb->buffer[tb->in] = t;
    // Avanza índice circular
    tb->in = (tb->in + 1) % MAX_TASK_BUFFER;
    // Incrementa contador de elementos
    tb->count++;
    pthread_cond_signal(&tb->not_empty);
    pthread_mutex_unlock(&tb->mux);
}

// Función para el consumidor: extrae una tarea del buffer
Task buffer_pop(TaskBuffer *tb) {
    Task t;
    // Bloquea el mutex antes de leer el buffer
    pthread_mutex_lock(&tb->mux);
    // Si el buffer está vacío, espera a que haya al menos un elemento
    while (tb->count == 0) {
        pthread_cond_wait(&tb->not_empty, &tb->mux);
    }
    // Extrae tarea de la posición "out"
    t = tb->buffer[tb->out];
    tb->out = (tb->out + 1) % MAX_TASK_BUFFER;
    // Decrementa contador
    tb->count--;
    pthread_cond_signal(&tb->not_full);
    pthread_mutex_unlock(&tb->mux);
  
    return t;
}
