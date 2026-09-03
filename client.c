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

    char buf[BUF_SIZE];
    int n = read(socket_fd, buf, sizeof(buf) - 1);

    if (n > 0)
    {
        buf[n] = '\0';
        printf("%s", buf);
    }
    else if (n == 0)
    {
        printf("[CLIENT] L'esecutore ha chiuso la connessione\n");
    }
    else
    {
        fatal("Errore in read");
    }
    close(socket_fd);
    return 0;
}