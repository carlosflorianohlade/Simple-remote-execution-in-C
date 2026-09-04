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

void fatal(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

void handle_sigTerm(int sig)
{
    terminating = 1;
}

void handle_sigchld(int sig)
{
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
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

    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));

    sa_chld.sa_handler = handle_sigchld;
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd == -1)
        fatal("Errore while initializing socket");

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    unlink(SOCKET_PATH); // Rimuove un eventuale socket rimasto da una precedente esecuzione

    if (bind(socket_fd, ((struct sockaddr *)&sa), sizeof(sa)) == -1)
        fatal("Errore in bind");
    if (listen(socket_fd, 10) == -1) // 16 è il numero di connessioni massime in sospeso
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
            close(fd_c);
            fatal("Errore in fork");
        }
        else if (pid == 0)
        {
            close(socket_fd); // Chiude il socket di ascolto del server

            char buf[BUF_SIZE];
            ssize_t bytes_read;

            printf("[ESECUTORE %d] Connesso al client, in ascolto...\n", getpid());

            while ((bytes_read = read(fd_c, buf, sizeof(buf) - 1)) > 0)
            {
                buf[bytes_read] = '\0';

                buf[strcspn(buf, "\r\n")] = '\0';

                if (strlen(buf) == 0)
                    continue;

                printf("[ESECUTORE %d] Ricevuto dal client \"%s\"\n", getpid(), buf);

                if (strcmp(buf, "exit") == 0)
                {
                    printf("[ESECUTORE %d] Ricevuto 'exit', termino.\n", getpid());
                    break;
                }

                // Risposta da mandare al client
                char response[BUF_SIZE + 32];
                snprintf(response, sizeof(response), "[ECHO DA ESECUTORE]: %s\n", buf);

                if (write(fd_c, response, strlen(response)) == -1)
                {
                    fatal("[ESECUTORE] Errore in write");
                }
            }

            if (bytes_read == 0)
            {
                printf("[ESECUTORE %d] Il client ha chiuso la connessione.\n", getpid());
            }
            else if (bytes_read == -1)
            {
                if (errno == EINTR && terminating)
                    printf("[ESECUTORE %d] Terminazione richiesta dal server, chiudo\n", getpid());
                else if (errno == EINTR)
                    printf("[ESECUTORE %d] Interrotto da segnale imprevisto, chiudo comunque\n", getpid());
                else
                    fatal("[ESECUTORE] Errore in read");
            }

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

    // Ignora SIGTERM per se stesso prima di segnalare il gruppo
    signal(SIGTERM, SIG_IGN);

    // Invia SIGTERM a tutti i processi del gruppo
    kill(0, SIGTERM);

    while (wait(NULL) > 0 || errno == EINTR)
        ;

    close(socket_fd);
    unlink(SOCKET_PATH);

    return 0;
}