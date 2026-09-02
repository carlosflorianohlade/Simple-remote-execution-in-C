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
    struct sockaddr_un sa, sa_client;

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

    return 0;
}