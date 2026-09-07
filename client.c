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
#define DELIMITER "\n---FINE_COMANDO---\n"

void fatal(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

int main()
{
    // Ignora segnali SIGINT, SIGQUIT e SIGPIPE come da specifiche
    struct sigaction sig_sa;
    memset(&sig_sa, '\0', sizeof(struct sigaction));
    sig_sa.sa_handler = SIG_IGN;

    if (sigaction(SIGINT, &sig_sa, NULL) == -1 ||
        sigaction(SIGQUIT, &sig_sa, NULL) == -1 ||
        sigaction(SIGPIPE, &sig_sa, NULL) == -1 ||
        sigaction(SIGTERM, &sig_sa, NULL) == -1)
    {
        fatal("Errore configurazione segnali");
    }

    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
        fatal("Errore nell'inizializzazione del socket");

    struct sockaddr_un sa;
    memset(&sa, '\0', sizeof(struct sockaddr_un));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCKET_PATH, sizeof(sa.sun_path) - 1);

    if (connect(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
        fatal("Errore nella connessione al server/esecutore");

    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];

    while (1)
    {
        printf("\nInserisci comando ('exit' per USCIRE): ");
        fflush(stdout);

        // Gestione EOF (ctrl+d)
        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL)
        {
            if (feof(stdin))
            {
                clearerr(stdin); // resetto l'indicatore di EOF
                printf("\n[CLIENT] Uscita consentità solo digitando 'exit'.\n");
                continue;
            }

            if (errno == EINTR)
                continue;

            fatal("[CLIENT] Errore in fgets");
        }

        // Rimuove terminatori di riga
        send_buf[strcspn(send_buf, "\r\n")] = '\0';

        // Verifica che la riga non sia vuota o composta solo da spazi/tab
        int solo_spazi = 1;
        for (int j = 0; send_buf[j] != '\0'; j++)
        {
            if (send_buf[j] != ' ' && send_buf[j] != '\t')
            {
                solo_spazi = 0;
                break;
            }
        }
        if (solo_spazi)
            continue;

        // Invio comando all'esecutore
        if (write(socket_fd, send_buf, strlen(send_buf)) == -1)
        {
            if (errno == EPIPE)
            {
                printf("[CLIENT] L'esecutore ha chiuso la connessione.\n");
                break;
            }
            fatal("[CLIENT] Errore invio comando");
        }

        // Se l'utente digita 'exit', termina localmente dopo l'invio
        if (strcmp(send_buf, "exit") == 0)
        {
            printf("[CLIENT] Chiusura in corso...\n");
            break;
        }

        // Ciclo di lettura della risposta fino al marcatore di fine output
        while (1)
        {
            ssize_t n = read(socket_fd, recv_buf, sizeof(recv_buf) - 1);
            if (n > 0)
            {
                recv_buf[n] = '\0';

                // Ricerca del delimitatore di fine comando
                char *delim_pos = strstr(recv_buf, DELIMITER);
                if (delim_pos != NULL)
                {
                    *delim_pos = '\0'; // Rimuove il marcatore prima della visualizzazione
                    printf("%s", recv_buf);
                    fflush(stdout);
                    break;
                }

                printf("%s", recv_buf);
                fflush(stdout);
            }
            else if (n == 0)
            {
                printf("[CLIENT] L'esecutore ha chiuso la connessione.\n");
                close(socket_fd);
                return 0;
            }
            else
            {
                if (errno == EINTR)
                    continue;
                fatal("[CLIENT] Errore nella lettura risposta");
            }
        }
    }

    close(socket_fd);
    return 0;
}