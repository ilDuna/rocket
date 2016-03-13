#include "rocket.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

static const char *P = "DC04EB6EB146437F17F6422B78DE6F7B"; /* 128-bit prime */
static const char *G = "03";

/*
 *  Server
 */

struct thread_arg {
    rocket_list_node **head;
    pthread_mutex_t *lock;
    uint16_t port;
    uint16_t cid;
    int ctrlsock;
};

void *rocket_tcp_task_open(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;
    uint16_t port = ((struct thread_arg *)arg)->port;

    printf("[server]\ttcp connection thread started.\n");
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
    printf("[server]\ttcp socket is listening on port %d\n", port);
    struct sockaddr_in clientaddr;
    int clientlen = sizeof(clientaddr);
    int activetcpsock = accept(tcpsock, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen);
    if (activetcpsock < 0) {
        rocket->tcp_task = 0;
        printf("[server]\ttcp socket [%d] accept error.\n", port);
        return NULL;
    }
    int recvbuffer = ROCK_TCP_RCVBUF;
    int sendbuffer = ROCK_TCP_SNDBUF;
    int keepalive = 1;
    setsockopt(activetcpsock, SOL_SOCKET, SO_RCVBUF, &recvbuffer, sizeof(recvbuffer));
    setsockopt(activetcpsock, SOL_SOCKET, SO_SNDBUF, &sendbuffer, sizeof(sendbuffer));
    setsockopt(activetcpsock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

    rocket->sd = activetcpsock;
    rocket->state = CONNECTED;
    rocket->tcp_task = 0;

    //TODO: signal anyone?
    printf("[server]\ttcp socket on port %d is CONNECTED w/ rocket session cid %d.\n", port, rocket->cid);
    rocket_list_print_item(rocket);
    int close_ret = close(tcpsock);
    if (close_ret < 0)
        printf("[server]\tfailed close on tcp socket file descriptor.\n");
    return NULL;
}

void *rocket_ctrl_listen(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;

    printf("[server]\tcontrol server started.\n");

    int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctrlsock < 0)
        printf("[server]\tctrl socket creation error.\n");
    int optval = 1;
    struct timeval tv;
    tv.tv_sec = ROCK_CTRLTIMEOUT;
    tv.tv_usec = 0;
    setsockopt(ctrlsock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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
            else if (rocket->state == CONNECTED)
                sendpkt->type = 100;
            else {
                rocket->buffer_size = recvpkt->buffer + ROCK_TCP_SNDBUF;
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
                sendpkt->buffer = ROCK_TCP_RCVBUF;

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
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_find(*head, recvpkt->cid);
            pthread_mutex_unlock(lock);
            if (rocket == 0 || (rocket!=0 && rocket->state == CLOSED))
                sendpkt->type = 100;
            else {
                sendpkt->type = 6;
                BN_rand(rocket->challenge, ROCK_DH_BIT, 0, 0);                  /* random challenge for authenticate client */
                sendpkt->k = BN_dup(rocket->challenge);             /* copy challenge into packet to send */
            }

            bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
            rocket_serialize_ctrlpkt(sendpkt, ctrlbuf);
            int m = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
            if (m < ROCK_CTRLPKTSIZE) {
                printf("[server]\tctrl socket send error.\n");
                continue;
            }

        }
        else if (recvpkt->type == 7) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_find(*head, recvpkt->cid);
            pthread_mutex_unlock(lock);
            if (rocket == 0 || (rocket!=0 && rocket->state == CLOSED))
                sendpkt->type = 100;
            else {
                BN_CTX *ctx = BN_CTX_new();
                BIGNUM *response = BN_new();
                BIGNUM *p_bn = BN_new();
                BN_hex2bn(&p_bn, P);
                BN_mod_exp(response, rocket->challenge, rocket->k, p_bn, ctx);    /* recalculate response = challenge^K mod p */
                if (BN_cmp(recvpkt->k, response) == 0) {      /* check if calculated response is equal to the received one */
                    sendpkt->type = 200;

                    pthread_t tid_tcpopen;
                    struct thread_arg *arg = malloc(sizeof(struct thread_arg));
                    arg->head = head;
                    arg->lock = lock;
                    arg->port = rocket->port;
                    rocket->tcp_task = 1;
                    pthread_create(&tid_tcpopen, NULL, rocket_tcp_task_open, arg); 
                }
                else
                    sendpkt->type = 100;        /* ah ah ah you didn't say magic word */
                BN_free(p_bn);
                BN_free(response);
                BN_CTX_free(ctx);
            }

            bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
            rocket_serialize_ctrlpkt(sendpkt, ctrlbuf);
            int m = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
            if (m < ROCK_CTRLPKTSIZE) {
                printf("[server]\tctrl socket send error.\n");
                continue;
            }
        }
        else if (recvpkt->type == 50) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_find(*head, recvpkt->cid);
            pthread_mutex_unlock(lock);
            if (rocket == 0)
                sendpkt->type = 100;
            else {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                rocket->lasthbtime = tv.tv_sec;
                if (rocket->state == SUSPENDED || rocket->state == CLOSED) {
                    rocket->state = AVAILABLE;
                    printf("[server]\trocket %d is AVAILABLE.\n", rocket->cid);
                }
                if (rocket->state == CONNECTED)
                    printf("[server]\trocket %d still CONNECTED.\n", rocket->cid);
                sendpkt->type = 200;
            }

            bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
            rocket_serialize_ctrlpkt(sendpkt, ctrlbuf);
            int m = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
            if (m < ROCK_CTRLPKTSIZE) {
                printf("[server]\tctrl socket send error.\n");
                continue;
            }
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

    printf("[server]\tnetwork monitor started.\n");

    while (1) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        pthread_mutex_lock(lock);
        if (*head != 0) {
            rocket_list_node *node;
            node = *head;
            int lastnode = 0;
            while (lastnode == 0) {
                if (node->rocket->lasthbtime != 0 && 
                    (node->rocket->state == CONNECTED || node->rocket->state == AVAILABLE) &&
                    tv.tv_sec - node->rocket->lasthbtime > ROCK_HB_RATE * ROCK_HB_MAXLOSS) {
                    
                    if (node->rocket->state == CONNECTED) {
                        node->rocket->state = SUSPENDED;
                        close(node->rocket->sd);    /* close current tcp socket */
                        //TODO: signal anyone? ------------------------------------
                    }
                    else {
                        node->rocket->state = SUSPENDED;
                    }
                    
                    printf("[server]\trocket %d is SUSPENDED.\n", node->rocket->cid);
                }
                if (node->next == 0)
                    lastnode = 1;
                node = node->next;
            }
        }
        pthread_mutex_unlock(lock);

        sleep(ROCK_NET_CHECK);
    }
    return NULL;
}

int rocket_ctrl_server(rocket_list_node **head, pthread_mutex_t *lock) {
    pthread_t tid_listen, tid_network;
    struct thread_arg *arg = malloc(sizeof(struct thread_arg));
    arg->head = head;
    arg->lock = lock;
    pthread_create(&tid_listen, NULL, rocket_ctrl_listen, arg);
    pthread_create(&tid_network, NULL, rocket_network_monitor, arg);
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
    rocket->challenge = BN_new();
    rocket->lasthbtime = 0;
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

void *rocket_client_network_monitor(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;
    uint16_t cid = ((struct thread_arg *)arg)->cid;
    uint16_t port = ((struct thread_arg *)arg)->port;
    int ctrlsock = ((struct thread_arg *)arg)->ctrlsock;

    printf("[client]\tnetwork monitor started.\n");

    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_find(*head, cid);
    pthread_mutex_unlock(lock);

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(ROCK_UDPPORT);
    inet_pton(AF_INET, (const char *)rocket->serveraddr, &serveraddr.sin_addr);
    while (1) {
        unsigned char ctrlbuf[ROCK_CTRLPKTSIZE];
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        rocket_ctrl_pkt pkt_hb;
        pkt_hb.type = 50;
        pkt_hb.cid = cid;
        rocket_serialize_ctrlpkt(&pkt_hb, ctrlbuf);
        int send_50 = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (send_50 != ROCK_CTRLPKTSIZE) {
            /* this section should not be called... */
            //rocket->state = SUSPENDED;
            //TODO: SIGNAL ANYONE????? ---------------------
            //printf("[client]\trocket %d is SUSPENDED.\n", rocket->cid);
        }
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        int recv_hbresp = recv(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        if (recv_hbresp < ROCK_CTRLPKTSIZE) {
            printf("[client]\trocket %d missed heartbeat.\n", rocket->cid);
            if (rocket->lasthbtime != 0 && 
                tv.tv_sec - rocket->lasthbtime > ROCK_HB_RATE * ROCK_HB_MAXLOSS) {

                rocket->state = SUSPENDED;

                close(rocket->sd);
                //TODO: SIGNAL ANYONE????? ---------------------
                printf("[client]\trocket %d is SUSPENDED.\n", rocket->cid);
            }
        }
        rocket_ctrl_pkt *pkt_hbresp = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_hbresp->type == 200 && rocket->state == SUSPENDED) {
            /* we can start the reconnection routine */
            int retry = ROCK_CTRLMAXRETRY;
            while(retry > 0) {
                int res = rocket_connect(1, head, rocket->serveraddr, port, lock);
                if (res == -1 && retry == 1) {
                    printf("[client]\trocket reconnection routine failed. Retrying in %d seconds.\n", (int)ROCK_HB_RATE);
                    break;
                }
                if (res == 0)
                    break;
                retry--;
                printf("[client]\trocket reconnection failed. Retrying...\n");
            }
        }
        else if (pkt_hbresp->type == 200 && rocket->state == CONNECTED) {
            rocket->lasthbtime = tv.tv_sec;
            rocket->state = CONNECTED;
            printf("[client]\trocket %d still CONNECTED.\n", rocket->cid);
        }

        sleep(ROCK_HB_RATE);
    }

    return NULL;
}

int rocket_connect(int reconnect, rocket_list_node **head, char *addr, uint16_t port, pthread_mutex_t *lock) {
    if (reconnect == 0) {
        int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctrlsock == -1)
            return -1;

        struct timeval tv;
        tv.tv_sec = ROCK_CTRLTIMEOUT;
        tv.tv_usec = 0;
        setsockopt(ctrlsock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(ctrlsock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in serveraddr;
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port = htons(ROCK_UDPPORT);
        inet_pton(AF_INET, addr, &serveraddr.sin_addr);
        unsigned char ctrlbuf[ROCK_CTRLPKTSIZE];

        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        rocket_ctrl_pkt pkt_1;
        pkt_1.type = 1;
        pkt_1.port = port;
        pkt_1.buffer = ROCK_TCP_RCVBUF;
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
        int recvbuffer = ROCK_TCP_RCVBUF;
        int sendbuffer = ROCK_TCP_SNDBUF;
        int keepalive = 1;
        setsockopt(tcpsock, SOL_SOCKET, SO_RCVBUF, &recvbuffer, sizeof(recvbuffer));
        setsockopt(tcpsock, SOL_SOCKET, SO_SNDBUF, &sendbuffer, sizeof(sendbuffer));
        setsockopt(tcpsock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
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
        rocket->buffer_size = buffer_size + ROCK_TCP_SNDBUF;
        rocket->lasthbtime = 0;
        rocket->serveraddr = addr;

        pthread_mutex_lock(lock);
        rocket_list_insert(head, rocket, cid);
        pthread_mutex_unlock(lock);

        pthread_t tid_netmonitor;
        struct thread_arg *arg = malloc(sizeof(struct thread_arg));
        arg->head = head;
        arg->lock = lock;
        arg->cid = cid;
        arg->ctrlsock = ctrlsock;
        arg->port = port;
        pthread_create(&tid_netmonitor, NULL, rocket_client_network_monitor, arg);
        rocket->cnet_monitor = tid_netmonitor;

        free(pkt_4);
        return 0;
    }
    else if (reconnect == 1) {
        int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctrlsock == -1)
            return -1;

        struct timeval tv;
        tv.tv_sec = ROCK_CTRLTIMEOUT;
        tv.tv_usec = 0;
        setsockopt(ctrlsock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(ctrlsock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in serveraddr;
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port = htons(ROCK_UDPPORT);
        inet_pton(AF_INET, addr, &serveraddr.sin_addr);
        unsigned char ctrlbuf[ROCK_CTRLPKTSIZE];

        pthread_mutex_lock(lock);
        rocket_t *rocket = rocket_list_findbyport(*head, port);
        pthread_mutex_unlock(lock);
        if (rocket == 0)
            return -1;

        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        rocket_ctrl_pkt pkt_5;
        pkt_5.type = 5;
        pkt_5.cid = rocket->cid;
        rocket_serialize_ctrlpkt(&pkt_5, ctrlbuf);
        int send_5 = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (send_5 != ROCK_CTRLPKTSIZE)
            return -1;
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        int recv_6 = recv(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0);
        if (recv_6 != ROCK_CTRLPKTSIZE)
            return -1;
        rocket_ctrl_pkt *pkt_6 = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_6->type == 100) {
            //TODO: this could be caused by a Server crash
            //      so we might need to initialize a NEW rocket connect!
            return -1;
        }
        rocket_ctrl_pkt pkt_7;
        pkt_7.type = 7;
        pkt_7.cid = rocket->cid;
        pkt_7.k = BN_new();
        BN_CTX *ctx = BN_CTX_new();
        BIGNUM *p_bn = BN_new();
        BN_hex2bn(&p_bn, P);
        BN_mod_exp(pkt_7.k, pkt_6->k, rocket->k, p_bn, ctx);        /* response = challenge^K mod p */
        BN_free(p_bn);
        BN_CTX_free(ctx);
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        rocket_serialize_ctrlpkt(&pkt_7, ctrlbuf);
        BN_free(pkt_7.k);
        int send_7 = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (send_7 != ROCK_CTRLPKTSIZE)
            return -1;
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        int recv_ = recv(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0);
        if (recv_ != ROCK_CTRLPKTSIZE)
            return -1;
        rocket_ctrl_pkt *pkt_ = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_6->type == 100)
            return -1;

        /* we can reconnect the tcp socket */
        int tcpsock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpsock < 0)
            return -1;
        int recvbuffer = ROCK_TCP_RCVBUF;
        int sendbuffer = ROCK_TCP_SNDBUF;
        int keepalive = 1;
        setsockopt(tcpsock, SOL_SOCKET, SO_RCVBUF, &recvbuffer, sizeof(recvbuffer));
        setsockopt(tcpsock, SOL_SOCKET, SO_SNDBUF, &sendbuffer, sizeof(sendbuffer));
        setsockopt(tcpsock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        struct sockaddr_in serveraddr_tcp;
        serveraddr_tcp.sin_family = AF_INET;
        serveraddr_tcp.sin_port = htons(port);
        inet_pton(AF_INET, addr, &serveraddr_tcp.sin_addr);
        int connect_ret = connect(tcpsock, (struct sockaddr *)&serveraddr_tcp, sizeof(serveraddr_tcp));
        if (connect_ret < 0)
            return -1;

        rocket->state = CONNECTED;
        rocket->sd = tcpsock;
        //TODO: SIGNAL ANYONE??????!???????


        return 0;
    }
    else
        return -1;
}

uint16_t rocket_client(rocket_list_node **head, char *addr, uint16_t port, pthread_mutex_t *lock) {
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
        sleep(1800); //just for debug sleep for 30min
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
