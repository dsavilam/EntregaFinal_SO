#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <time.h>

#define MAX_TITLE_LEN    100
#define MAX_LINE_LEN     256
#define MAX_TASK_BUFFER  10
#define MAX_EJEMPLARES   20
#define DATE_STR_LEN     11
#define FIFO_NAME_LEN    64

typedef enum {
    OP_PRESTAMO,
    OP_RENOVAR,
    OP_DEVOLVER,
    OP_SALIR
} OpType;

// Petición recibida
typedef struct {
    OpType op;
    char title[MAX_TITLE_LEN];
    int isbn;
} Request;

// Registro de log
typedef struct LogEntry {
    char status;
    char title[MAX_TITLE_LEN];
    int isbn;
    int ejemplar;
    char date[DATE_STR_LEN];
    struct LogEntry *next;
} LogEntry;

// Ejemplar de un libro
typedef struct {
    int id;
    char status;
    char date[DATE_STR_LEN];
} Ejemplar;

// Libro completo
typedef struct {
    char title[MAX_TITLE_LEN];
    int isbn;
    int total;
    Ejemplar ejemplares[MAX_EJEMPLARES];
} Book;

// Nodo de lista enlazada de libros
typedef struct BookNode {
    Book book;
    struct BookNode *next;
} BookNode;

// Tarea para buffer
typedef struct {
    OpType op;
    int isbn;
    int ejemplar;
} Task;

// Buffer circular
typedef struct {
    Task buffer[MAX_TASK_BUFFER];
    int in, out, count;
    pthread_mutex_t mux;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} TaskBuffer;

// --- Declaraciones `extern` ---
// Buffer global
extern TaskBuffer task_buffer;
// Puntero al inicio de la lista de libros
extern BookNode *db_head;
// Mutex que protege la BD
extern pthread_mutex_t db_mux;
// Puntero al inicio de la lista de logs
extern LogEntry *log_head;
// Mutex que protege la lista de logs
extern pthread_mutex_t log_mux;

#endif // COMMON_H
