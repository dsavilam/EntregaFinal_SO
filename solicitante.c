// solicitante.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include "common.h"

static char fifo_name[FIFO_NAME_LEN]; // Nombre del FIFO (mi_fifo)

/*
 * Modo interactivo:
 *   - P/R/D: pide Título e ISBN → envía "Op,Título,ISBN\n" → lee respuesta real.
 *   - Q: envía "Q,Salir,0\n", lee "BYE\n", luego sale.
 */
void interactive(int fd) {
    while (1) {
        printf("Operación (P=Préstamo, R=Renovar, D=Devolver, Q=Salir): ");
        char op = getchar();
        /* Consumir resto de línea */
        while (getchar() != '\n');

        /* Normalizar a mayúscula */
        if (op >= 'a' && op <= 'z') {
            op -= 32;
        }
        if (op != 'P' && op != 'R' && op != 'D' && op != 'Q') {
            printf("Opción no válida. Intente de nuevo.\n");
            continue;
        }

        if (op == 'Q') {
            /* Enviar cierre */
            char msg[] = "Q,Salir,0\n";
            write(fd, msg, strlen(msg));
            /* Leer respuesta "BYE\n" real */
            char resp[MAX_LINE_LEN];
            while (1) {
                ssize_t n = read(fd, resp, sizeof(resp) - 1);
                if (n <= 0) continue;
                resp[n] = '\0';
                /* Si es “Q,Salir,0” (eco), se ignora */
                if (resp[0] == 'Q') continue;
                /* Llegó BYE */
                printf("%s", resp);
                break;
            }
            break;
        }

        /* Para P/R/D pedimos título e ISBN */
        printf("Título: ");
        char title[MAX_TITLE_LEN];
        fgets(title, sizeof(title), stdin);
        title[strcspn(title, "\n")] = '\0';  // eliminar '\n'

        printf("ISBN: ");
        int isbn;
        if (scanf("%d", &isbn) != 1) {
            printf("ISBN inválido. Use sólo números.\n");
            while (getchar() != '\n');  // desechar línea
            continue;
        }
        while (getchar() != '\n');  // desechar '\n'

        /* Enviar “Op,Título,ISBN\n” */
        char msg[MAX_LINE_LEN];
        snprintf(msg, sizeof(msg), "%c,%s,%d\n", op, title, isbn);
        write(fd, msg, strlen(msg));

        /* Luego, leer la respuesta real: “OK...” o “FAIL...” */
        char resp[MAX_LINE_LEN];
        while (1) {
            ssize_t n = read(fd, resp, sizeof(resp) - 1);
            if (n <= 0) continue;
            resp[n] = '\0';
            /* Si es eco “P,…” o “R,…” o “D,…” lo ignoramos */
            char c0 = resp[0];
            if (c0 == 'P' || c0 == 'R' || c0 == 'D' || c0 == 'Q') {
                continue;
            }
            /* Llegó “OK,…\n” o “FAIL,…\n” */
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

    /* Abrir el mismo FIFO una sola vez en modo O_RDWR */
    int fd = open(fifo_name, O_RDWR);
    if (fd < 0) {
        perror("Error al abrir el FIFO");
        exit(1);
    }

    if (use_file) {
        /* Versión “desde archivo” (opcional, no la usaremos para ahora) */
        FILE *f = fopen(file_arg, "r");
        if (!f) {
            perror("Error al abrir archivo de peticiones");
            close(fd);
            exit(1);
        }
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || strlen(line) <= 1) continue;
            write(fd, line, strlen(line));
            /* Leer respuesta real (ignorar ecos) */
            char resp[MAX_LINE_LEN];
            while (1) {
                ssize_t n = read(fd, resp, sizeof(resp) - 1);
                if (n <= 0) continue;
                resp[n] = '\0';
                if (resp[0] == 'P' || resp[0] == 'R' || resp[0] == 'D' || resp[0] == 'Q')
                    continue;
                printf("Respuesta: %s", resp);
                if (strncmp(resp, "BYE", 3) == 0) {
                    fclose(f);
                    close(fd);
                    return 0;
                }
                break;
            }
            if (line[0] == 'Q') break;
        }
        fclose(f);
    } else {
        interactive(fd);
    }

    close(fd);
    return 0;
}
