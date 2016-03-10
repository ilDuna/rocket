#include "rocket.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>

#define LOCALHOST       "127.0.0.1"

static const char *P = "DC04EB6EB146437F17F6422B78DE6F7B"; /* 128-bit prime */
static const char *G = "03";

/*
 *  Server
 */

struct thread_arg {
    rocket_list_node **head;
    pthread_mutex_t *lock;
    uint16_t port;
};

void *rocket_tcp_task_open(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;
    uint16_t port = ((struct thread_arg *)arg)->port;

    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_findbyport(*head, port);
    pthread_mutex_unlock(lock);
    if (rocket == 0)
        return NULL;

    int tcpsock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpsock < 0) {
        rocket->tcp_task = 0;
        printf("[server]\ttcp socket [%d] creation error.\n", port);
        return NULL;
    }
    int optval = 1;
    setsockopt(tcpsock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in serveraddr;
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(port);
    int bind_ret = bind(tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (bind_ret < 0) {
        rocket->tcp_task = 0;
        printf("[server]\ttcp socket [%d] binding error.\n", port);
        return NULL;
    }
    int listen_ret = listen(tcpsock, 5);
    if (listen_ret < 0) {
        rocket->tcp_task = 0;
        printf("[server]\ttcp socket [%d] listen error.\n", port);
        return NULL;
    }
    struct sockaddr_in clientaddr;
    int clientlen = sizeof(clientaddr);
    int activetcpsock = accept(tcpsock, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen);
    if (activetcpsock < 0) {
        rocket->tcp_task = 0;
        printf("[server]\ttcp socket [%d] accept error.\n", port);
        return NULL;
    }
    rocket->sd = activetcpsock;
    rocket->state = CONNECTED;
    rocket->tcp_task = 0;

    //TODO: signal anyone?
    printf("[server]\ttcp socket at port %d connected at rocket session cid %d.\n", port, rocket->cid);
    rocket_list_print_item(rocket);
    int close_ret = close(tcpsock);
    if (close_ret < 0)
        printf("[server]\tfailed close on tcp socket file descriptor.\n");
    return NULL;
}

void *rocket_ctrl_listen(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;

    int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctrlsock < 0)
        printf("[server]\tctrl socket creation error.\n");
    int optval = 1;
    setsockopt(ctrlsock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in serveraddr;
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((uint16_t)ROCK_UDPPORT);
    int bind_ret = bind(ctrlsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (bind_ret < 0)
        printf("[server]\tctrl socket binding error.\n");
    unsigned int clientlen;
    unsigned char ctrlbuf[ROCK_CTRLPKTSIZE];
    struct sockaddr_in clientaddr;
    clientlen = sizeof(clientaddr);
    while(1) {
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        int recv_ret = recvfrom(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, &clientlen);
        if (recv_ret < ROCK_CTRLPKTSIZE) {
            printf("[server]\tctrl socket recv error.\n");
            continue;
        }
        rocket_ctrl_pkt *recvpkt = rocket_deserialize_ctrlpkt(ctrlbuf);
        rocket_ctrl_pkt *sendpkt = malloc(sizeof(rocket_ctrl_pkt));
        sendpkt->k = BN_new();

        if (recvpkt->type == 1) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_findbyport(*head, recvpkt->port);
            pthread_mutex_unlock(lock);
            if (rocket == 0)
                sendpkt->type = 100;
            else if (rocket->state == CONNECTED || rocket->state == SUSPENDED)
                sendpkt->type = 100;
            else {
                rocket->buffer_size = recvpkt->buffer;
                BN_rand(rocket->a, ROCK_DH_BIT, 0, 0);                  /* random private key a */
                BN_CTX *ctx = BN_CTX_new();
                BIGNUM *g_bn = BN_new();
                BIGNUM *p_bn = BN_new();
                BN_hex2bn(&g_bn, G);
                BN_hex2bn(&p_bn, P);
                BN_mod_exp(sendpkt->k, g_bn, rocket->a, p_bn, ctx);     /* A = g^a mod p (to send in clear) */
                BN_free(g_bn);
                BN_free(p_bn);
                BN_CTX_free(ctx);

                sendpkt->type = 2;
            }

            bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
            rocket_serialize_ctrlpkt(sendpkt, ctrlbuf);
            int m = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
            if (m < ROCK_CTRLPKTSIZE) {
                printf("[server]\tctrl socket send error.\n");
                continue;
            }
        }
        else if (recvpkt->type == 3) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_findbyport(*head, recvpkt->port);
            pthread_mutex_unlock(lock);
            if (rocket == 0)
                sendpkt->type = 100;
            else if (rocket->state == CONNECTED || rocket->state == SUSPENDED)
                sendpkt->type = 100;
            else {
                BN_CTX *ctx = BN_CTX_new();
                BIGNUM *g_bn = BN_new();
                BIGNUM *p_bn = BN_new();
                BN_hex2bn(&g_bn, G);
                BN_hex2bn(&p_bn, P);
                BN_mod_exp(rocket->k, recvpkt->k, rocket->a, p_bn, ctx);    /* K = B^a mod p (shared private key) */
                BN_free(g_bn);
                BN_free(p_bn);
                BN_CTX_free(ctx);

                sendpkt->type = 4;
                sendpkt->cid = rocket->cid;
                sendpkt->buffer = 65000; /* TODO: get real tcp receive buffer size! */

                pthread_t tid_tcpopen;
                struct thread_arg *arg = malloc(sizeof(struct thread_arg));
                arg->head = head;
                arg->lock = lock;
                arg->port = recvpkt->port;
                rocket->tcp_task = 1;
                pthread_create(&tid_tcpopen, NULL, rocket_tcp_task_open, arg);
            }

            bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
            rocket_serialize_ctrlpkt(sendpkt, ctrlbuf);
            int m = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
            if (m < ROCK_CTRLPKTSIZE) {
                printf("[server]\tctrl socket send error.\n");
                continue;
            }
        }
        else if (recvpkt->type == 5) {

        }
        else if (recvpkt->type == 7) {

        }
        
        BN_free(recvpkt->k);
        BN_free(sendpkt->k);
        free(recvpkt);
        free(sendpkt);
    }
    return NULL;
}

void *rocket_network_monitor(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;
    /* monitor heartbeats and do things */
    return NULL;
}

int rocket_ctrl_server(rocket_list_node **head, pthread_mutex_t *lock) {
    pthread_t tid_listen, tid_network;
    struct thread_arg *arg = malloc(sizeof(struct thread_arg));
    arg->head = head;
    arg->lock = lock;
    pthread_create(&tid_listen, NULL, rocket_ctrl_listen, arg);
    //pthread_create(&tid_network, NULL, rocket_network_monitor, arg);
    //TODO: remove joins. Leave it here just for debug purpose
    pthread_join(tid_listen, NULL);
    //pthread_join(tid_network, NULL);
    return 0;
}

uint16_t rocket_server(rocket_list_node **head, uint16_t port, pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_findbyport(*head, port);
    pthread_mutex_unlock(lock);
    if (rocket != 0)
        return rocket->cid;
    /* TODO: check if port is used by other apps */
    rocket = malloc(sizeof(rocket_t));
    
    rocket->role = SERVER;
    rocket->state = CLOSED;
    rocket->sd = 0;
    rocket->port = port;
    rocket->tcp_task = 0;
    rocket->a = BN_new();
    rocket->k = BN_new();
    int cid_decided = 0;
    uint16_t cid = 0;
    srand(time(NULL));
    while (cid_decided == 0) {
        cid = (uint16_t)rand() % 65535;
        rocket_t *tmp = rocket_list_find(*head, cid);
        if (tmp == 0)
            cid_decided++;
    }
    rocket->cid = cid;

    pthread_mutex_lock(lock);
    rocket_list_insert(head, rocket, rocket->cid);
    pthread_mutex_unlock(lock);
    return 0;
}


/*
 *  Client
 */

int rocket_connect(int reconnect, rocket_list_node **head, const char *addr, uint16_t port, pthread_mutex_t *lock) {
    if (reconnect == 0) {
        int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctrlsock == -1)
            return -1;
        struct sockaddr_in serveraddr;
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port = htons(ROCK_UDPPORT);
        inet_pton(AF_INET, addr, &serveraddr.sin_addr);
        unsigned char ctrlbuf[ROCK_CTRLPKTSIZE];

        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        rocket_ctrl_pkt pkt_1;
        pkt_1.type = 1;
        pkt_1.port = port;
        pkt_1.buffer = 65000;   /* TODO: get real tcp receive buffer size */
        rocket_serialize_ctrlpkt(&pkt_1, ctrlbuf);
        int send_1 = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (send_1 != ROCK_CTRLPKTSIZE)
            return -1;
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        int recv_2 = recv(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0);
        if (recv_2 != ROCK_CTRLPKTSIZE)
            return -1;
        rocket_ctrl_pkt *pkt_2 = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_2->type == 100)
            return -1;
        rocket_ctrl_pkt pkt_3;
        pkt_3.type = 3;
        pkt_3.port = port;
        pkt_3.k = BN_new();
        BIGNUM *k = BN_new();
        BIGNUM *b = BN_new();
        BN_rand(b, ROCK_DH_BIT, 0, 0);              /* random private key b */
        BN_CTX *ctx = BN_CTX_new();
        BIGNUM *g_bn = BN_new();
        BIGNUM *p_bn = BN_new();
        BN_hex2bn(&g_bn, G);
        BN_hex2bn(&p_bn, P);
        BN_mod_exp(pkt_3.k, g_bn, b, p_bn, ctx);    /* B = g^b mod p (to send in clear) */
        BN_mod_exp(k, pkt_2->k, b, p_bn, ctx);      /* K = A^b mod p (shared private key) */
        BN_free(g_bn);
        BN_free(p_bn);
        BN_free(b);
        BN_CTX_free(ctx);
        BN_free(pkt_2->k);
        free(pkt_2);
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        rocket_serialize_ctrlpkt(&pkt_3, ctrlbuf);
        BN_free(pkt_3.k);
        int send_3 = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (send_3 != ROCK_CTRLPKTSIZE)
            return -1;
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        int recv_4 = recv(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0);
        if (recv_4 != ROCK_CTRLPKTSIZE)
            return -1;
        rocket_ctrl_pkt *pkt_4 = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_4->type == 100)
            return -1;
        uint16_t cid = pkt_4->cid;
        uint32_t buffer_size = pkt_4->buffer;

        int tcpsock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpsock < 0)
            return -1;
        struct sockaddr_in serveraddr_tcp;
        serveraddr_tcp.sin_family = AF_INET;
        serveraddr_tcp.sin_port = htons(port);
        inet_pton(AF_INET, addr, &serveraddr_tcp.sin_addr);
        int connect_ret = connect(tcpsock, (struct sockaddr *)&serveraddr_tcp, sizeof(serveraddr_tcp));
        if (connect_ret < 0)
            return -1;

        rocket_t *rocket = malloc(sizeof(rocket_t));
        rocket->role = CLIENT;
        rocket->cid = cid;
        rocket->a = BN_new();
        rocket->k = k;
        rocket->state = CONNECTED;
        rocket->sd = tcpsock;
        rocket->port = port;
        rocket->buffer_size = buffer_size+65000; //TODO: + host receive buffer size

        pthread_mutex_lock(lock);
        rocket_list_insert(head, rocket, cid);
        pthread_mutex_unlock(lock);

        free(pkt_4);
        return 0;
    }
    else if (reconnect == 1) {

        return 0;
    }
    else
        return -1;
}

uint16_t rocket_client(rocket_list_node **head, const char *addr, uint16_t port, pthread_mutex_t *lock) {
    int retry = ROCK_CTRLMAXRETRY;
    while(retry > 0) {
        int res = rocket_connect(0, head, addr, port, lock);
        if (res == -1 && retry == 1)
            return 0;
        if (res == 0)
            break;
        retry--;
        printf("[client]\trocket establishment failed. Retrying...\n");
    }
    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_findbyport(*head, port);
    pthread_mutex_unlock(lock);
    printf("[client]\trocket established at %s:%d.\n", addr, port);
    /* start thread to manage heartbeats and connection recovery */
    return rocket->cid;
}

int main(int argc, char *argv[]) {
    if (argc > 3 && strcmp(argv[1], "-c")==0) {
        printf("--client mode--\n");
        pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(lock, NULL);
        rocket_list_node *head = 0;
        rocket_client(&head, argv[2], (uint16_t)atoi(argv[3]), lock);
        rocket_list_print(head);
    }
    else if (argc > 1 && strcmp(argv[1], "-s")==0) {
        printf("--server mode--\n");
        pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(lock, NULL);
        rocket_list_node *head = 0;
        rocket_server(&head, 125, lock);
        rocket_server(&head, 126, lock);
        rocket_ctrl_server(&head, lock);
    } 
    else
        printf("usage: rocket [-c server_ip_address tcp_port] [-s]\n");

	return 0;
}
