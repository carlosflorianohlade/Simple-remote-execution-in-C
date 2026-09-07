#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define SOCKET_PATH "/tmp/socket"
#define BUF_SIZE 2048
#define MAX_ARGS 64
#define DELIMITER "\n---FINE_COMANDO---\n"

volatile sig_atomic_t terminating = 0;

void fatal(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

void handle_sigTerm(int sig)
{
    (void)sig;
    terminating = 1;
}

void handle_sigchld(int sig)
{
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

int main()
{
    // Rende il server leader di un nuovo process group indipendente
    setpgid(0, 0);

    int socket_fd;
    struct sockaddr_un sa;
    memset(&sa, '\0', sizeof(struct sockaddr_un));

    // 1. Ignora SIGINT, SIGQUIT e SIGPIPE come da specifiche
    struct sigaction sig_sa;
    memset(&sig_sa, '\0', sizeof(struct sigaction));
    sig_sa.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &sig_sa, NULL) == -1 ||
        sigaction(SIGQUIT, &sig_sa, NULL) == -1 ||
        sigaction(SIGPIPE, &sig_sa, NULL) == -1)
    {
        fatal("Errore configurazione segnali ignorati");
    }

    // 2. Gestione SIGTERM per terminazione controllata
    struct sigaction sig_term;
    memset(&sig_term, '\0', sizeof(struct sigaction));
    sig_term.sa_handler = handle_sigTerm;
    if (sigaction(SIGTERM, &sig_term, NULL) == -1)
        fatal("Errore sigaction SIGTERM");

    // 3. Gestione SIGCHLD per evitare zombie degli esecutori nel server
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = handle_sigchld;
    sa_chld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1)
        fatal("Errore sigaction SIGCHLD");

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
        fatal("Errore inizializzazione socket");

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    unlink(SOCKET_PATH);

    if (bind(socket_fd, ((struct sockaddr *)&sa), sizeof(sa)) == -1)
        fatal("Errore in bind");
    if (listen(socket_fd, 16) == -1)
        fatal("Errore in listen");

    printf("[SERVER %d] In ascolto su %s (termina con SIGTERM)...\n", getpid(), SOCKET_PATH);

    while (!terminating)
    {
        int fd_c = accept(socket_fd, NULL, NULL);
        if (fd_c == -1)
        {
            if (errno == EINTR)
                continue;
            fatal("Errore in accept");
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            close(fd_c);
            fatal("Errore in fork server");
        }
        else if (pid == 0)
        {
            /* ==============================================
             * PROCESSO ESECUTORE
             * ============================================== */

            // Ripristina la gestione di default di SIGCHLD per poter fare waitpid() sincrona
            if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
                fatal("Errore ripristino SIGCHLD esecutore");

            close(socket_fd); // Chiude il socket di ascolto del server

            char buf[BUF_SIZE];
            ssize_t bytes_read;

            printf("[ESECUTORE %d] Connesso al client, in ascolto comandi...\n", getpid());

            while (!terminating && (bytes_read = read(fd_c, buf, sizeof(buf) - 1)) > 0)
            {
                buf[bytes_read] = '\0';
                buf[strcspn(buf, "\r\n")] = '\0';

                // Parsing del comando e degli argomenti con strtok
                char *args[MAX_ARGS];
                int i = 0;
                char *token = strtok(buf, " \t");
                while (token != NULL && i < MAX_ARGS - 1)
                {
                    args[i++] = token;
                    token = strtok(NULL, " \t");
                }
                args[i] = NULL;

                // Se la stringa era vuota o di soli spazi, risponde per non bloccare il client
                if (i == 0)
                {
                    write(fd_c, DELIMITER, strlen(DELIMITER));
                    continue;
                }

                // Comando di chiusura esplicito dal client
                if (strcmp(args[0], "exit") == 0)
                {
                    printf("[ESECUTORE %d] Ricevuto 'exit', chiusura sessione.\n", getpid());
                    break;
                }

                pid_t pid_cmd = fork();
                if (pid_cmd < 0)
                {
                    char *err_fork = "Errore: impossibile creare processo comando.\n";
                    write(fd_c, err_fork, strlen(err_fork));
                    write(fd_c, DELIMITER, strlen(DELIMITER));
                    continue;
                }
                else if (pid_cmd == 0)
                {
                    // Reindirizza STDOUT e STDERR sul socket verso il client tramite dup
                    close(STDOUT_FILENO);
                    if (dup(fd_c) == -1)
                        fatal("Errore dup stdout");

                    close(STDERR_FILENO);
                    if (dup(fd_c) == -1)
                        fatal("Errore dup stderr");

                    close(fd_c);

                    execvp(args[0], args);

                    // Se execvp fallisce (es. comando inesistente)
                    perror("Errore esecuzione comando");
                    exit(EXIT_FAILURE);
                }
                else
                {
                    int status;
                    while (waitpid(pid_cmd, &status, 0) == -1)
                    {
                        if (errno == EINTR && terminating)
                            break;
                    }

                    // Se il server ha ordinato la chiusura mentre aspettavamo il comando, esce subito
                    if (terminating)
                        break;

                    // Invia il marcatore di fine output al client
                    write(fd_c, DELIMITER, strlen(DELIMITER));
                }
            }

            if (bytes_read == 0)
            {
                printf("[ESECUTORE %d] Il client ha chiuso la connessione.\n", getpid());
            }
            else if (terminating)
            {
                printf("[ESECUTORE %d] Terminazione richiesta dal server, chiudo.\n", getpid());
            }

            close(fd_c);
            exit(EXIT_SUCCESS);
        }
        else
        {
            // Processo server genitore
            printf("[SERVER] Connessione assegnata all'Esecutore PID %d\n", pid);
            close(fd_c);
        }
    }

    /* ==============================================
     * FASE DI TERMINAZIONE SERVER
     * ============================================== */
    printf("\n[SERVER] Ricevuto SIGTERM: avvio terminazione ordinata...\n");

    // Disattiva il signal handler di SIGCHLD per evitare conflitti con la wait bloccante finale
    signal(SIGCHLD, SIG_DFL);

    // Ignora SIGTERM per se stesso per non auto-interrompersi
    signal(SIGTERM, SIG_IGN);

    // Invia SIGTERM a tutti i processi figli nel gruppo
    kill(0, SIGTERM);

    // Attende la terminazione di TUTTI gli esecutori figli
    while (wait(NULL) > 0 || errno == EINTR)
        ;

    close(socket_fd);
    unlink(SOCKET_PATH);

    printf("[SERVER] Chiusura completata con successo.\n");
    return 0;
}