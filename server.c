#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>

#define SOCKET_PATH "/tmp/socket"
#define BUF_SIZE 2048

volatile sig_atomic_t terminating = 0;

void fatal(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

void handle_sigTerm(int iSignum)
{
    terminating = 1;
}

int main()
{
    int socket_fd;
    struct sockaddr_un sa;
    memset(&sa, '\0', sizeof(struct sockaddr_un));

    // segnale per SIGINT e SIGQUIT
    struct sigaction sig_sa;
    memset(&sig_sa, '\0', sizeof(struct sigaction)); // azzero tutta la struct sigaction

    // ignoro i segnali SIGINT e SIGQUIT
    sig_sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sig_sa, NULL);
    sigaction(SIGQUIT, &sig_sa, NULL);
    sigaction(SIGPIPE, &sig_sa, NULL);

    // segnale per SIGTERM
    struct sigaction sig_term;
    memset(&sig_term, '\0', sizeof(struct sigaction)); // azzero tutta la struct sigaction

    sig_term.sa_handler = handle_sigTerm;
    sigaction(SIGTERM, &sig_term, NULL);

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd == -1)
        fatal("Errore while initializing socket");

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    unlink(SOCKET_PATH); // se il server termina per errore, rimuovo il socket prima di avviare la connessione

    if (bind(socket_fd, ((struct sockaddr *)&sa), sizeof(sa)) == -1)
        fatal("Errore in bind");
    if (listen(socket_fd, 16) == -1) // 16 è il numero di connessioni massime in sospeso
        fatal("Errore in listen");

    while (!terminating)
    {
        int fd_c = accept(socket_fd, NULL, NULL);
        if (fd_c == -1)
        {
            if (errno == EINTR)
                continue;
            fatal("Errore in accept");
        }

        pid_t pid;
        pid = fork();

        if (pid < 0)
        {
            fatal("Errore in fork");
        }
        else if (pid == 0)
        {
            close(socket_fd);

            char welcome[BUF_SIZE];
            snprintf(welcome, sizeof(welcome), "Benvenuto! sei connesso all'esecutore con PID %d\n", getpid());
            write(fd_c, welcome, strlen(welcome));

            printf("[ESECUTORE %d] Connessione gestita, in attesa di comandi...\n", getpid());

            close(fd_c);
            exit(EXIT_SUCCESS);
        }
        else
        {
            // server
            printf("[SERVER] Nuova connessione, creato esecutore PID %d\n", pid);
            close(fd_c);
        }
    }

    printf("[SERVER] Terminazione richiesta, chiudo\n");
    close(socket_fd);
    unlink(SOCKET_PATH);

    return 0;
}