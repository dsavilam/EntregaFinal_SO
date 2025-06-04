// receptor.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <getopt.h>
#include "common.h"
#include "buffer.h"
#include "db.h"

static char fifo_name[FIFO_NAME_LEN];   
static char db_filename[128];          
static char out_filename[128];          
static int verbose = 0;                 
static int keep_running = 1;            

/*
 * Hilo que procesa en segundo plano las tareas de renovación y devolución
 * que se encolan en task_buffer.
 */
void* aux1_thread(void* arg) {
    while (1) {
        Task t = buffer_pop(&task_buffer);
        if (t.op == OP_SALIR) {
            /* Si recibe OP_SALIR, sale del hilo */
            break;
        }
        if (t.op == OP_DEVOLVER) {
            do_devolver(t.isbn, t.ejemplar);
        } else if (t.op == OP_RENOVAR) {
            char dummy_date[DATE_STR_LEN];
            do_renovar(t.isbn, t.ejemplar, dummy_date);
        }
    }
    return NULL;
}

/*
 * Hilo que atiende comandos locales en receptor:
 *   - 'r': imprime reporte de logs (print_report)
 *   - 's': guarda BD final (save_db) y ordena cierre de todo el receptor
 */
void* aux2_thread(void* arg) {
    while (keep_running) {
        char cmd = getchar();
        if (cmd == 'r') {
            print_report();
        } else if (cmd == 's') {
            keep_running = 0;  // Señal para terminar
            Task t = { .op = OP_SALIR };
            buffer_push(&task_buffer, t);
            if (out_filename[0]) {
                save_db(out_filename);
                if (verbose) {
                    printf("Guardada BD en \"%s\" y receptor cerrándose (comando 's').\n",
                           out_filename);
                }
            }
            break;
        }
    }
    return NULL;
}

void handle_request(Request* req, int client_fd) {
    char response[MAX_LINE_LEN];

    /* 1) Si es OP_SALIR (Q), devolvemos "BYE\n" y regresamos */
    if (req->op == OP_SALIR) {
        snprintf(response, sizeof(response), "BYE\n");
        write(client_fd, response, strlen(response));
        return;
    }

    /* 2) Buscar el libro por ISBN */
    BookNode* bn = find_book(req->isbn);
    if (!bn) {
        /* ISBN no existe → FAIL,NoExiste */
        snprintf(response, sizeof(response),
                 "FAIL,NoExiste,%d\n",
                 req->isbn);
        write(client_fd, response, strlen(response));
        if (verbose) {
            printf("Manejada operación [X] \"NoExiste\" (ISBN: %d)\n", req->isbn);
        }
        return;
    }

    /* 3) Validar que el título coincide EXACTO */
    if (strcmp(req->title, bn->book.title) != 0) {
        snprintf(response, sizeof(response),
                 "FAIL,NoExiste,%d\n",
                 req->isbn);
        write(client_fd, response, strlen(response));
        if (verbose) {
            printf("Manejada operación [X] \"NoExiste\" (ISBN: %d)\n", req->isbn);
        }
        return;
    }

    /* 4) Ya sabemos que ISBN y título son correctos; aplicamos la operación */
    if (req->op == OP_PRESTAMO) {
        int ejemplar;
        char due_date[DATE_STR_LEN];
        if (do_prestamo(req->isbn, &ejemplar, due_date) == 0) {
            snprintf(response, sizeof(response),
                     "OK,Prestado,%d,%d,%s\n",
                     req->isbn, ejemplar, due_date);
        } else {
            snprintf(response, sizeof(response),
                     "FAIL,NoDisponible,%d\n",
                     req->isbn);
        }
        write(client_fd, response, strlen(response));
    }
    else if (req->op == OP_RENOVAR) {
        int ejemplar = -1;
        for (int i = 0; i < bn->book.total; i++) {
            if (bn->book.ejemplares[i].status == 'P') {
                ejemplar = bn->book.ejemplares[i].id;
                break;
            }
        }
        char new_date[DATE_STR_LEN];
        if (ejemplar >= 0 && do_renovar(req->isbn, ejemplar, new_date) == 0) {
            snprintf(response, sizeof(response),
                     "OK,Renovado,%d,%d,%s\n",
                     req->isbn, ejemplar, new_date);
            /* Encolar la tarea real para que aux1_thread actualice la BD */
            Task t = { .op = OP_RENOVAR, .isbn = req->isbn, .ejemplar = ejemplar };
            buffer_push(&task_buffer, t);
        } else {
            snprintf(response, sizeof(response),
                     "FAIL,NoExiste,%d\n",
                     req->isbn);
        }
        write(client_fd, response, strlen(response));
    }
    else if (req->op == OP_DEVOLVER) {
        int ejemplar = -1;
        for (int i = 0; i < bn->book.total; i++) {
            if (bn->book.ejemplares[i].status == 'P') {
                ejemplar = bn->book.ejemplares[i].id;
                break;
            }
        }
        if (ejemplar >= 0 && do_devolver(req->isbn, ejemplar) == 0) {
            snprintf(response, sizeof(response),
                     "OK,Devuelto,%d,%d\n",
                     req->isbn, ejemplar);
            Task t = { .op = OP_DEVOLVER, .isbn = req->isbn, .ejemplar = ejemplar };
            buffer_push(&task_buffer, t);
        } else {
            snprintf(response, sizeof(response),
                     "FAIL,NoExiste,%d\n",
                     req->isbn);
        }
        write(client_fd, response, strlen(response));
    }

    if (verbose) {
        char op_char = '?';
        if (req->op == OP_PRESTAMO)    op_char = 'P';
        else if (req->op == OP_RENOVAR) op_char = 'R';
        else if (req->op == OP_DEVOLVER)op_char = 'D';
        else if (req->op == OP_SALIR)   op_char = 'Q';
        printf("Manejada operación [%c] \"%s\" (ISBN: %d)\n",
               op_char, req->title, req->isbn);
    }
}

int main(int argc, char* argv[]) {
    int opt;
    char pipe_arg[64] = {0};
    char file_arg[128] = {0};
    char out_arg[128] = {0};

    /*
     * Parsear opciones:
     *   -p <fifo>   → nombre del pipe_FIFO
     *   -f <file>   → archivo de BD inicial
     *   -v          → modo verbose
     *   -s <file>   → archivo BD final al cerrar
     */
    while ((opt = getopt(argc, argv, "p:f:vs:")) != -1) {
        switch (opt) {
            case 'p': strncpy(pipe_arg, optarg, sizeof(pipe_arg)); break;
            case 'f': strncpy(file_arg, optarg, sizeof(file_arg)); break;
            case 'v': verbose = 1; break;
            case 's': strncpy(out_arg, optarg, sizeof(out_arg)); break;
            default:
                fprintf(stderr,
                        "Uso: %s -p pipeReceptor -f filedatos [-v] [-s filesalida]\n",
                        argv[0]);
                exit(1);
        }
    }

    if (!pipe_arg[0] || !file_arg[0]) {
        fprintf(stderr, "Error: faltan parámetros obligatorios.\n");
        exit(1);
    }
    strncpy(fifo_name, pipe_arg, FIFO_NAME_LEN);
    strncpy(db_filename, file_arg, sizeof(db_filename));
    if (out_arg[0]) {
        strncpy(out_filename, out_arg, sizeof(out_filename));
    }

    /* 1) Cargar la BD inicial */
    load_db(db_filename);

    /* 2) Inicializar buffer de tareas y lanzar hilos auxiliares */
    buffer_init(&task_buffer);
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, aux1_thread, NULL);
    pthread_create(&tid2, NULL, aux2_thread, NULL);

    /* 3) Crear el FIFO (o reutilizar si ya existe) */
    mkfifo(fifo_name, 0666);

    /* 4) Abrir el FIFO en O_RDWR (para leer y escribir) */
    int fd = open(fifo_name, O_RDWR);
    if (fd < 0) {
        perror("Error al abrir el FIFO");
        exit(1);
    }

    /* 5) Bucle infinito atendiendo peticiones */
    while (keep_running) {
        char buf[MAX_LINE_LEN];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            continue;  /* si no llegó nada, vuelvo a leer */
        }
        buf[n] = '\0';

        /* 5.1) Parsear “Op,Title,ISBN\n” */
        Request req;
        char* t0 = strtok(buf, ",");
        if (!t0) continue;
        char op_char = t0[0];
        /* Normalizar a mayúscula */
        if (op_char >= 'a' && op_char <= 'z') {
            op_char -= 32;
        }
        if (op_char == 'P')       req.op = OP_PRESTAMO;
        else if (op_char == 'R')  req.op = OP_RENOVAR;
        else if (op_char == 'D')  req.op = OP_DEVOLVER;
        else                      req.op = OP_SALIR;

        /* 5.2) Extraer Título (sin espacios al inicio) */
        char* t1 = strtok(NULL, ",");
        if (t1) {
            while (*t1 == ' ') t1++;
            strncpy(req.title, t1, MAX_TITLE_LEN);
            /* 5.3) Extraer ISBN */
            char* t2 = strtok(NULL, ",");
            if (t2) {
                while (*t2 == ' ') t2++;
                req.isbn = atoi(t2);
            }
        }

        /* 6) Ejecutar la petición concreta */
        handle_request(&req, fd);
    }

    /* 7) Esperar a que terminen los hilos auxiliares antes de salir */
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    /* 8) Cerrar y borrar el FIFO */
    close(fd);
    unlink(fifo_name);
    return 0;
}
