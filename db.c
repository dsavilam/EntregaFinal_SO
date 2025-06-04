// db.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "db.h"

TaskBuffer task_buffer;           
BookNode *db_head = NULL;        
pthread_mutex_t db_mux = PTHREAD_MUTEX_INITIALIZER;
LogEntry *log_head = NULL;        
pthread_mutex_t log_mux = PTHREAD_MUTEX_INITIALIZER;


// Función interna que formatea un time_t a string "DD-MM-YYYY"
static void format_date(time_t t, char out_date[]) {
    struct tm *tm_info = localtime(&t);
    strftime(out_date, DATE_STR_LEN, "%d-%m-%Y", tm_info);
}

// Carga la base de datos desde un archivo de texto.
void load_db(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Error al abrir archivo de base de datos");
        exit(1);
    }
    char line[MAX_LINE_LEN];

    // Lee línea a línea
    while (fgets(line, sizeof(line), f)) {
        // Si la línea está vacía o sólo newline, saltar
        if (strlen(line) <= 1) continue;

        // Crear nuevo nodo para este libro
        BookNode *bn = malloc(sizeof(BookNode));
        memset(bn, 0, sizeof(BookNode));

        // Primera línea del libro: "Título,ISBN,Total"
        char *tok = strtok(line, ",");
        strncpy(bn->book.title, tok, MAX_TITLE_LEN);

        tok = strtok(NULL, ",");
        bn->book.isbn = atoi(tok);

        tok = strtok(NULL, ",");
        bn->book.total = atoi(tok);

        // Leer exactamente 'total' líneas siguientes, cada una describe un ejemplar
        for (int i = 0; i < bn->book.total; i++) {
            if (!fgets(line, sizeof(line), f)) break;
            char *t2 = strtok(line, ",");
            bn->book.ejemplares[i].id = atoi(t2);

            t2 = strtok(NULL, ",");
            bn->book.ejemplares[i].status = t2[1]; 

            t2 = strtok(NULL, ",");
            strncpy(bn->book.ejemplares[i].date, t2, DATE_STR_LEN);
            bn->book.ejemplares[i].date[
                strcspn(bn->book.ejemplares[i].date, "\r\n")
            ] = '\0';
        }

        // Insertar al inicio de la lista enlazada (orden arbitrario)
        bn->next = db_head;
        db_head = bn;
    }

    fclose(f);
}

// Guarda la BD actual de vuelta a un archivo de texto.
void save_db(const char *filename) {
    pthread_mutex_lock(&db_mux);

    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Error al abrir archivo de salida de base de datos");
        pthread_mutex_unlock(&db_mux);
        return;
    }

    // Recorre cada nodo/libro en memoria
    for (BookNode *bn = db_head; bn; bn = bn->next) {
        // Escribe línea de cabecera: "Título,ISBN,Total"
        fprintf(f, "%s,%d,%d\n",
                bn->book.title,
                bn->book.isbn,
                bn->book.total);

        // Escribe cada ejemplar en su propia línea
        for (int i = 0; i < bn->book.total; i++) {
            Ejemplar *e = &bn->book.ejemplares[i];

            // Antes de imprimir, recortamos cualquier '\r' o '\n' sobrante
            e->date[ strcspn(e->date, "\r\n") ] = '\0';

            fprintf(f, "%d, %c, %s\n",
                    e->id,
                    e->status,
                    e->date);
        }
    }

    fclose(f);
    pthread_mutex_unlock(&db_mux);
}

// Busca un libro por ISBN en la lista enlazada.
BookNode* find_book(int isbn) {
    for (BookNode *bn = db_head; bn; bn = bn->next) {
        if (bn->book.isbn == isbn) {
            return bn;
        }
    }
    return NULL;
}

// Busca un ejemplar disponible ('D') en el arreglo de un Book.
int find_available_ejemplar(Book *b) {
    for (int i = 0; i < b->total; i++) {
        if (b->ejemplares[i].status == 'D') {
            return i;
        }
    }
    return -1;
}

// Realiza el préstamo de un ejemplar de un libro con el ISBN dado.
int do_prestamo(int isbn, int *out_ejemplar, char out_date[]) {
    pthread_mutex_lock(&db_mux);

    BookNode *bn = find_book(isbn);
    if (!bn) {
        // Libro no existe
        pthread_mutex_unlock(&db_mux);
        return -1;
    }
    // Buscar ejemplar libre
    int idx = find_available_ejemplar(&bn->book);
    if (idx < 0) {
        // No hay ejemplar disponible
        pthread_mutex_unlock(&db_mux);
        return -1;
    }

    // Calcular fecha de devolución: hoy + 7 días
    time_t now = time(NULL);
    time_t future = now + 7 * 24 * 3600;
    format_date(future, out_date);

    // Actualizar ejemplar: marcar prestado
    bn->book.ejemplares[idx].status = 'P';
    strncpy(bn->book.ejemplares[idx].date, out_date, DATE_STR_LEN);

    // Devolver ID de ejemplar al llamador
    *out_ejemplar = bn->book.ejemplares[idx].id;

    // Agregar registro en log
    add_log('P', bn->book.title, isbn, bn->book.ejemplares[idx].id, out_date);

    pthread_mutex_unlock(&db_mux);
    return 0;
}

// Realiza renovación de un ejemplar específico de un libro.
int do_renovar(int isbn, int ejemplar, char out_date[]) {
    pthread_mutex_lock(&db_mux);

    BookNode *bn = find_book(isbn);
    if (!bn) {
        // Libro no existe
        pthread_mutex_unlock(&db_mux);
        return -1;
    }
    // Buscar ejemplar en el arreglo
    for (int i = 0; i < bn->book.total; i++) {
        if (bn->book.ejemplares[i].id == ejemplar &&
            bn->book.ejemplares[i].status == 'P') {
            // Calcular nueva fecha: hoy + 7 días
            time_t now = time(NULL);
            time_t future = now + 7 * 24 * 3600;
            format_date(future, out_date);

            // Actualizar fecha en ejemplar
            strncpy(bn->book.ejemplares[i].date, out_date, DATE_STR_LEN);

            // Agregar registro de renovación en log
            add_log('R', bn->book.title, isbn, ejemplar, out_date);

            pthread_mutex_unlock(&db_mux);
            return 0;
        }
    }

    // Ejemplar no encontrado o no está prestado
    pthread_mutex_unlock(&db_mux);
    return -1;
}

// Realiza devolución de un ejemplar prestado.
int do_devolver(int isbn, int ejemplar) {
    pthread_mutex_lock(&db_mux);

    BookNode *bn = find_book(isbn);
    if (!bn) {
        // Libro no existe
        pthread_mutex_unlock(&db_mux);
        return -1;
    }
    for (int i = 0; i < bn->book.total; i++) {
        if (bn->book.ejemplares[i].id == ejemplar &&
            bn->book.ejemplares[i].status == 'P') {
            // Cambiar status a disponible
            bn->book.ejemplares[i].status = 'D';

            // Fijar fecha de devolución (fecha actual)
            time_t now = time(NULL);
            char today[DATE_STR_LEN];
            format_date(now, today);
            strncpy(bn->book.ejemplares[i].date, today, DATE_STR_LEN);

            // Agregar registro de devolución en log
            add_log('D', bn->book.title, isbn, ejemplar, today);

            pthread_mutex_unlock(&db_mux);
            return 0;
        }
    }

    // Ejemplar no encontrado o no estaba prestado
    pthread_mutex_unlock(&db_mux);
    return -1;
}

// Agrega un nuevo nodo LogEntry al inicio de la lista de logs.
// status: 'P','R' o 'D'; title: título del libro; isbn: ISBN; ejemplar: número; date: fecha "DD-MM-YYYY"
void add_log(char status, const char *title, int isbn, int ejemplar, const char date[]) {
    // Reservar memoria para nuevo registro
    LogEntry *n = malloc(sizeof(LogEntry));
    n->status = status;
    strncpy(n->title, title, MAX_TITLE_LEN);
    n->isbn = isbn;
    n->ejemplar = ejemplar;
    strncpy(n->date, date, DATE_STR_LEN);

    // Proteger con mutex mientras modificamos la lista
    pthread_mutex_lock(&log_mux);
    n->next = log_head;
    log_head = n;
    pthread_mutex_unlock(&log_mux);
}

// Imprime por pantalla todos los registros de la lista de logs.
void print_report() {
    pthread_mutex_lock(&log_mux);
    for (LogEntry *le = log_head; le; le = le->next) {
        printf("%c, %s, %d, %d, %s\n",
               le->status,
               le->title,
               le->isbn,
               le->ejemplar,
               le->date);
    }
    pthread_mutex_unlock(&log_mux);
}
