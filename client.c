#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/socket"
#define BUF_SIZE 2048

void fatal(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

int main()
{
    struct sigaction sig_sa;
    memset(&sig_sa, '\0', sizeof(struct sigaction));

    // ignoro i segnali SIGINT e SIGQUIT
    sig_sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sig_sa, NULL);
    sigaction(SIGQUIT, &sig_sa, NULL);
    sigaction(SIGPIPE, &sig_sa, NULL);

    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
        fatal("errore nell'inizializzazione del socket");

    struct sockaddr_un sa;
    memset(&sa, '\0', sizeof(struct sockaddr_un));

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    if (connect(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
        fatal("Errore nella connessione");

    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];

    while (1)
    {
        printf("\nInserisci comando ('exit' per USCIRE): ");
        fflush(stdout);

        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL)
        {
            printf("\nRilevato EOF. Invio 'exit' all'esecutore e chiudo...\n");
            write(socket_fd, "exit\n", 5);
            break;
        }

        // gestione invio a vuoto
        if (strcmp(send_buf, "\n") == 0)
            continue;

        send_buf[strcspn(send_buf, "\r\n")] = '\0';

        if (write(socket_fd, send_buf, strlen(send_buf)) == -1)
        {
            if (errno == EPIPE)
            {
                printf("[CLIENT] L'esecutore ha chiuso la connessione.\n");
                close(socket_fd);
                return 0;
            }

            fatal("[CLIENT] Errore nell'invio del comando");
        }

        if (strcmp(send_buf, "exit") == 0)
        {
            printf("[CLIENT] Comando 'exit' inviato. Chiusura in corso...\n");
            break;
        }

        // Attesa risposta esecutore
        ssize_t n = read(socket_fd, recv_buf, sizeof(recv_buf) - 1);
        if (n > 0)
        {
            recv_buf[n] = '\0';
            printf("%s", recv_buf);
        }
        else if (n == 0)
        {
            printf("[CLIENT] L'esecutore ha chiuso la connessione.\n");
            break;
        }
        else
            fatal("[CLIENT] Errore nella lettura della risposta");
    }

    close(socket_fd);
    return 0;
}