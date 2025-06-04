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

/* Hilo auxiliar que procesa en segundo plano las tareas de renovación y devolución */
void* aux1_thread(void* arg) {
    while (1) {
        Task t = buffer_pop(&task_buffer);
        if (t.op == OP_SALIR) {
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

/* Hilo auxiliar que atiende comandos de reporte ('r') o cierre ('s') desde stdin */
void* aux2_thread(void* arg) {
    while (keep_running) {
        char cmd = getchar();
        if (cmd == 'r') {
            print_report();
        } else if (cmd == 's') {
            keep_running = 0;
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

/* Procesa una petición del solicitante según OP_PRESTAMO, OP_RENOVAR, OP_DEVOLVER o OP_SALIR */
void handle_request(Request* req, int client_fd) {
    char response[MAX_LINE_LEN];

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
    } else if (req->op == OP_RENOVAR) {
        int ejemplar = -1;
        BookNode* bn = find_book(req->isbn);
        if (bn) {
            for (int i = 0; i < bn->book.total; i++) {
                if (bn->book.ejemplares[i].status == 'P') {
                    ejemplar = bn->book.ejemplares[i].id;
                    break;
                }
            }
        }
        char new_date[DATE_STR_LEN];
        if (ejemplar >= 0 && do_renovar(req->isbn, ejemplar, new_date) == 0) {
            snprintf(response, sizeof(response),
                     "OK,Renovado,%d,%d,%s\n",
                     req->isbn, ejemplar, new_date);
            Task t = { .op = OP_RENOVAR, .isbn = req->isbn, .ejemplar = ejemplar };
            buffer_push(&task_buffer, t);
        } else {
            snprintf(response, sizeof(response),
                     "FAIL,NoExiste,%d\n",
                     req->isbn);
        }
        write(client_fd, response, strlen(response));
    } else if (req->op == OP_DEVOLVER) {
        int ejemplar = -1;
        BookNode* bn = find_book(req->isbn);
        if (bn) {
            for (int i = 0; i < bn->book.total; i++) {
                if (bn->book.ejemplares[i].status == 'P') {
                    ejemplar = bn->book.ejemplares[i].id;
                    break;
                }
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
    } else if (req->op == OP_SALIR) {
        snprintf(response, sizeof(response), "BYE\n");
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

/* Función principal: configura argumentos, carga BD, lanza hilos y atiende peticiones */
int main(int argc, char* argv[]) {
    int opt;
    char pipe_arg[64] = {0};
    char file_arg[128] = {0};
    char out_arg[128] = {0};

    while ((opt = getopt(argc, argv, "p:f:vs:")) != -1) {
        switch (opt) {
            case 'p':
                strncpy(pipe_arg, optarg, sizeof(pipe_arg));
                break;
            case 'f':
                strncpy(file_arg, optarg, sizeof(file_arg));
                break;
            case 'v':
                verbose = 1;
                break;
            case 's':
                strncpy(out_arg, optarg, sizeof(out_arg));
                break;
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

    load_db(db_filename);

    buffer_init(&task_buffer);
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, aux1_thread, NULL);
    pthread_create(&tid2, NULL, aux2_thread, NULL);

    mkfifo(fifo_name, 0666);

    int fd = open(fifo_name, O_RDWR);
    if (fd < 0) {
        perror("Error al abrir el FIFO");
        exit(1);
    }

    while (keep_running) {
        char buf[MAX_LINE_LEN];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            continue;
        }
        buf[n] = '\0';

        Request req;
        char* t0 = strtok(buf, ",");
        if (!t0) continue;
        if (t0[0] == 'P')       req.op = OP_PRESTAMO;
        else if (t0[0] == 'R')  req.op = OP_RENOVAR;
        else if (t0[0] == 'D')  req.op = OP_DEVOLVER;
        else                    req.op = OP_SALIR;

        char* t1 = strtok(NULL, ",");
        if (t1) {
            strncpy(req.title, t1, MAX_TITLE_LEN);
            char* t2 = strtok(NULL, ",");
            if (t2) req.isbn = atoi(t2);
        }

        handle_request(&req, fd);
    }

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    close(fd);
    unlink(fifo_name);
    return 0;
}
