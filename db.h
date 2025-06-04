#ifndef DB_H
#define DB_H

#include "common.h"

// Carga el archivo de texto que tenemos como bden memoria.
void load_db(const char *filename);

// Guarda el contenido actual de la BD en el archivo de texto.
// Se utiliza al finalizar el servicio.
void save_db(const char *filename);

// Busca un libro en la lista enlazada por su ISBN.
// Devuelve puntero al nodo BookNode si lo encuentra, o NULL si no.
BookNode* find_book(int isbn);

// Busca un ejemplar disponible ('D') dentro de un Book.
int find_available_ejemplar(Book *b);

// Realiza la operación de préstamo haciendo las validaciones
int do_prestamo(int isbn, int *out_ejemplar, char out_date[]);

// Realiza la operación de renovación de un ejemplar específico con las verificaciones
int do_renovar(int isbn, int ejemplar, char out_date[]);

// Realiza la devolución de un ejemplar con las verificaciones
int do_devolver(int isbn, int ejemplar);


// Agregamos registros al log con status: 'P', 'R' o 'D'; title: título del libro; isbn: ISBN; ejemplar: número; date: fecha operación.
void add_log(char status, const char *title, int isbn, int ejemplar, const char date[]);

void print_report();

#endif 
