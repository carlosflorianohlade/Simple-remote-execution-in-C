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

void fatal(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
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
    /* Client ed esecutore devono ignorare SIGINT e SIGQUIT. */
    struct sigaction sig_sa;
    memset(&sig_sa, 0, sizeof(sig_sa));
    sig_sa.sa_handler = SIG_IGN;

    if (sigaction(SIGINT, &sig_sa, NULL) == -1 ||
        sigaction(SIGQUIT, &sig_sa, NULL) == -1 ||
        sigaction(SIGPIPE, &sig_sa, NULL) == -1)
    {
        fatal("Errore configurazione segnali");
    }

    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
        fatal("Errore inizializzazione socket");

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    if (connect(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
        fatal("Errore nella connessione");

    char send_buf[BUF_SIZE];

    while (1)
    {
        printf("\nInserisci comando ('exit' per USCIRE): ");
        fflush(stdout);

        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL)
        {
            if (feof(stdin))
            {
                printf("\n[CLIENT] Inserire 'exit' per terminare.\n");
                clearerr(stdin);
                continue;
            }

            if (errno == EINTR)
                continue;

            fatal("[CLIENT] Errore in fgets");
        }

        send_buf[strcspn(send_buf, "\r\n")] = '\0';

        if (send_buf[0] == '\0')
            continue;

        int solo_spazi = 1;

        for (int i = 0; send_buf[i] != '\0'; i++)
        {
            if (send_buf[i] != ' ' && send_buf[i] != '\t')
            {
                solo_spazi = 0;
                break;
            }
        }

        if (solo_spazi)
            continue;

        if (write_all(socket_fd, send_buf, strlen(send_buf)) == -1)
        {
            if (errno == EPIPE)
            {
                printf("[CLIENT] L'esecutore ha chiuso la connessione.\n");
                break;
            }

            fatal("[CLIENT] Errore invio comando");
        }

        if (strcmp(send_buf, "exit") == 0)
        {
            printf("[CLIENT] Chiusura in corso...\n");
            break;
        }

        /*
         * L'esecutore invia l'output seguito dal delimitatore.
         * Non assumiamo che una singola read() contenga tutta la risposta.
         */
        const char *delimiter = "---FINE_COMANDO---\n";
        size_t delimiter_len = strlen(delimiter);
        size_t matched = 0;
        char c;

        while (1)
        {
            ssize_t n = read(socket_fd, &c, 1);

            if (n == 0)
            {
                printf("[CLIENT] L'esecutore ha chiuso la connessione.\n");
                close(socket_fd);
                return 0;
            }

            if (n == -1)
            {
                if (errno == EINTR)
                    continue;

                fatal("[CLIENT] Errore lettura risposta");
            }

            if (c == delimiter[matched])
            {
                matched++;

                if (matched == delimiter_len)
                    break;
            }
            else
            {
                if (matched > 0)
                {
                    fwrite(delimiter, 1, matched, stdout);
                    matched = 0;
                }

                putchar(c);
                fflush(stdout);
            }
        }
    }

    close(socket_fd);
    return 0;
}