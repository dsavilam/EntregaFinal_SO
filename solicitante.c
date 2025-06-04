// solicitante.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include "common.h"

static char fifo_name[FIFO_NAME_LEN];

/* Lee un archivo de peticiones y descarta los ecos propios hasta recibir la respuesta real */
void from_file(const char *fname, int fd) {
    FILE *f = fopen(fname, "r");
    if (!f) {
        perror("Error al abrir archivo de peticiones");
        exit(1);
    }
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || strlen(line) <= 1) {
            continue;
        }
        write(fd, line, strlen(line));

        char resp[MAX_LINE_LEN];
        while (1) {
            ssize_t n = read(fd, resp, sizeof(resp) - 1);
            if (n <= 0) {
                continue;
            }
            resp[n] = '\0';
            char c0 = resp[0];
            if (c0 == 'P' || c0 == 'R' || c0 == 'D' || c0 == 'Q') {
                continue;
            }
            printf("Respuesta: %s", resp);
            /* Si el servidor respondió BYE, salimos */
            if (strncmp(resp, "BYE", 3) == 0) {
                fclose(f);
                return;
            }
            break;
        }

        if (line[0] == 'Q') {
            break;
        }
    }
    fclose(f);
}

/* Modo interactivo: envía P/R/D/Q y descarta ecos hasta leer OK/FAIL/BYE */
void interactive(int fd) {
    while (1) {
        printf("Operación (P=Préstamo, R=Renovar, D=Devolver, Q=Salir): ");
        char op = getchar();
        while (getchar() != '\n');

        if (op != 'P' && op != 'R' && op != 'D' && op != 'Q') {
            printf("Opción no válida. Intente de nuevo.\n");
            continue;
        }

        if (op == 'Q') {
            char msg[] = "Q,Salir,0\n";
            write(fd, msg, strlen(msg));
            char resp[MAX_LINE_LEN];
            while (1) {
                ssize_t n = read(fd, resp, sizeof(resp) - 1);
                if (n <= 0) continue;
                resp[n] = '\0';
                if (resp[0] == 'Q') {
                    continue;
                }
                printf("%s", resp);
                /* BYE: terminamos */
                break;
            }
            break;
        }

        printf("Título: ");
        char title[MAX_TITLE_LEN];
        fgets(title, sizeof(title), stdin);
        title[strcspn(title, "\n")] = '\0';

        printf("ISBN: ");
        int isbn;
        if (scanf("%d", &isbn) != 1) {
            printf("ISBN inválido. Use sólo números.\n");
            while (getchar() != '\n') {}
            continue;
        }
        while (getchar() != '\n') {}

        char msg[MAX_LINE_LEN];
        snprintf(msg, sizeof(msg), "%c,%s,%d\n", op, title, isbn);
        write(fd, msg, strlen(msg));

        char resp[MAX_LINE_LEN];
        while (1) {
            ssize_t n = read(fd, resp, sizeof(resp) - 1);
            if (n <= 0) continue;
            resp[n] = '\0';
            char c0 = resp[0];
            if (c0 == 'P' || c0 == 'R' || c0 == 'D' || c0 == 'Q') {
                continue;
            }
            printf("Respuesta: %s", resp);
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    int opt;
    int use_file = 0;
    char file_arg[128] = {0};

    while ((opt = getopt(argc, argv, "i:p:")) != -1) {
        switch (opt) {
            case 'i':
                use_file = 1;
                strncpy(file_arg, optarg, sizeof(file_arg));
                break;
            case 'p':
                strncpy(fifo_name, optarg, FIFO_NAME_LEN);
                break;
            default:
                fprintf(stderr, "Uso: %s [-i archivo] -p pipeReceptor\n", argv[0]);
                exit(1);
        }
    }
    if (!fifo_name[0]) {
        fprintf(stderr, "Error: debe especificar el pipe del receptor (-p).\n");
        exit(1);
    }

    int fd = open(fifo_name, O_RDWR);
    if (fd < 0) {
        perror("Error al abrir el FIFO");
        exit(1);
    }

    if (use_file) {
        from_file(file_arg, fd);
    } else {
        interactive(fd);
    }

    close(fd);
    return 0;
}
