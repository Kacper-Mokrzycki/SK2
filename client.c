#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#include "common.h"

static int sockfd;
static pthread_t recv_thread_id;
static int running = 1;

// Funkcja wątku odbierającego komunikaty z serwera
void* recv_thread(void *arg) {
    char buffer[BUF_SIZE];
    while(running) {
        memset(buffer, 0, BUF_SIZE);
        int bytes = recv(sockfd, buffer, BUF_SIZE - 1, 0);
        if(bytes <= 0) {
            printf("Utracono polaczenie z serwerem.\n");
            running = 0;
            break;
        }
        printf("[SERWER] %s", buffer);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    if(argc < 3) {
        printf("Uzycie: %s <adres IP> <port>\n", argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);

    // Tworzymy socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Łączymy się z serwerem
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Polaczono z serwerem %s:%d\n", ip, port);

    // Uruchamiamy wątek odbierający
    pthread_create(&recv_thread_id, NULL, recv_thread, NULL);

    // W pętli czytamy z stdin i wysyłamy do serwera
    char input[BUF_SIZE];
    while(running) {
        if(!fgets(input, BUF_SIZE, stdin)) {
            break;
        }
        // Usunięcie znaku nowej linii
        input[strcspn(input, "\n")] = 0;

        if(strncmp(input, "QUIT", 4) == 0) {
            send(sockfd, "QUIT\n", 5, 0);
            running = 0;
            break;
        } else if(strncmp(input, "MOVE", 4) == 0) {
            // Format: MOVE from_x from_y to_x to_y
            // Przykład: MOVE 2 3 3 4
            char buf[BUF_SIZE];
            snprintf(buf, BUF_SIZE, "%s\n", input);
            send(sockfd, buf, strlen(buf), 0);
        } else {
            // Nieznana komenda, ale wysyłamy
            char buf[BUF_SIZE];
            snprintf(buf, BUF_SIZE, "%s\n", input);
            send(sockfd, buf, strlen(buf), 0);
        }
    }

    // Kończymy
    pthread_cancel(recv_thread_id);
    pthread_join(recv_thread_id, NULL);

    close(sockfd);
    return 0;
}
