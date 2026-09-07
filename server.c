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
#define DELIMITER "---FINE_COMANDO---\n"

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

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

int write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = write(fd, buf + sent, len - sent);

        if (n > 0)
            sent += (size_t)n;
        else if (n == -1 && errno == EINTR)
            continue;
        else
            return -1;
    }

    return 0;
}

int main()
{
    /* Il server crea un process group dedicato, ereditato dagli esecutori. */
    if (setpgid(0, 0) == -1)
        fatal("Errore setpgid");

    int socket_fd;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));

    struct sigaction sig_sa;
    memset(&sig_sa, 0, sizeof(sig_sa));
    sig_sa.sa_handler = SIG_IGN;

    if (sigaction(SIGINT, &sig_sa, NULL) == -1 ||
        sigaction(SIGQUIT, &sig_sa, NULL) == -1 ||
        sigaction(SIGPIPE, &sig_sa, NULL) == -1)
    {
        fatal("Errore configurazione segnali ignorati");
    }

    struct sigaction sig_term;
    memset(&sig_term, 0, sizeof(sig_term));
    sig_term.sa_handler = handle_sigTerm;

    /* Niente SA_RESTART: SIGTERM deve interrompere accept(). */
    if (sigaction(SIGTERM, &sig_term, NULL) == -1)
        fatal("Errore sigaction SIGTERM");

    struct sigaction sig_chld;
    memset(&sig_chld, 0, sizeof(sig_chld));
    sig_chld.sa_handler = handle_sigchld;
    sig_chld.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sig_chld, NULL) == -1)
        fatal("Errore sigaction SIGCHLD");

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
        fatal("Errore inizializzazione socket");

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    unlink(SOCKET_PATH);

    if (bind(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
        fatal("Errore in bind");

    if (listen(socket_fd, 16) == -1)
        fatal("Errore in listen");

    printf("[SERVER %d] In ascolto su %s (termina con SIGTERM)...\n",
           getpid(), SOCKET_PATH);

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

        if (pid == 0)
        {
            /* ================= ESECUTORE ================= */

            /* Il server ha un SIGCHLD handler, ma l'esecutore
             * deve poter usare waitpid() sul processo comando. */
            if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
                fatal("Errore ripristino SIGCHLD esecutore");

            close(socket_fd);

            char buf[BUF_SIZE];
            ssize_t bytes_read = -1;

            printf("[ESECUTORE %d] Connesso al client, in ascolto...\n",
                   getpid());

            while (!terminating &&
                   (bytes_read = read(fd_c, buf, sizeof(buf) - 1)) > 0)
            {
                buf[bytes_read] = '\0';

                char *args[MAX_ARGS];
                int i = 0;

                char *token = strtok(buf, " \t\r\n");

                while (token != NULL && i < MAX_ARGS - 1)
                {
                    args[i++] = token;
                    token = strtok(NULL, " \t\r\n");
                }

                args[i] = NULL;

                if (i == 0)
                    continue;

                if (strcmp(args[0], "exit") == 0)
                {
                    printf("[ESECUTORE %d] Ricevuto 'exit', chiusura.\n",
                           getpid());
                    break;
                }

                pid_t pid_cmd = fork();

                if (pid_cmd < 0)
                {
                    const char *msg =
                        "Errore: impossibile creare processo comando.\n";

                    if (write_all(fd_c, msg, strlen(msg)) == -1 ||
                        write_all(fd_c, DELIMITER, strlen(DELIMITER)) == -1)
                    {
                        break;
                    }

                    continue;
                }

                if (pid_cmd == 0)
                {
                    /* stdout e stderr del comando vanno al client. */
                    if (dup2(fd_c, STDOUT_FILENO) == -1)
                        exit(EXIT_FAILURE);

                    if (dup2(fd_c, STDERR_FILENO) == -1)
                        exit(EXIT_FAILURE);

                    close(fd_c);

                    execvp(args[0], args);

                    perror("Errore esecuzione comando");
                    exit(EXIT_FAILURE);
                }

                int status;

                while (waitpid(pid_cmd, &status, 0) == -1)
                {
                    if (errno == EINTR)
                    {
                        if (terminating)
                            break;

                        continue;
                    }

                    break;
                }

                if (terminating)
                    break;

                if (write_all(fd_c, DELIMITER, strlen(DELIMITER)) == -1)
                    break;
            }

            if (bytes_read == 0)
            {
                printf("[ESECUTORE %d] Client disconnesso.\n", getpid());
            }
            else if (terminating)
            {
                printf("[ESECUTORE %d] Terminazione richiesta dal server.\n",
                       getpid());
            }
            else if (bytes_read == -1)
            {
                if (errno == EINTR && terminating)
                    printf("[ESECUTORE %d] Terminazione richiesta dal server.\n",
                           getpid());
                else
                    perror("[ESECUTORE] Errore in read");
            }

            close(fd_c);
            return 0;
        }

        /* ================= SERVER ================= */

        printf("[SERVER] Connessione assegnata all'Esecutore PID %d\n", pid);
        close(fd_c);
    }

    /* ================= TERMINAZIONE SERVER ================= */

    printf("\n[SERVER] Ricevuto SIGTERM: terminazione ordinata...\n");

    /*
     * Gli esecutori appartengono allo stesso process group del server.
     * Il server ignora SIGTERM, mentre gli esecutori lo ricevono.
     */
    signal(SIGTERM, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);

    if (kill(0, SIGTERM) == -1)
        perror("[SERVER] Errore invio SIGTERM agli esecutori");

    while (wait(NULL) > 0 || errno == EINTR);

    close(socket_fd);
    unlink(SOCKET_PATH);

    printf("[SERVER] Chiusura completata.\n");
    return 0;
}