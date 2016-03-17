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

/********************************************/
/************* SERVER FUNCTIONS *************/
/********************************************/

/* used to pass arguments to threads */
struct thread_arg {
    rocket_list_node **head;
    pthread_mutex_t *lock;
    uint16_t port;
    uint16_t cid;
    int ctrlsock;
};

/* open a tcp socket and wait for a connection, then save
 * the socket file descriptor and put the rocket state to
 * connected */
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
    /* enable SO_REUSEADDR option on the tcp socket */
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
    /* set the SEND and RECV buffer and TIMEOUT (to 0) and enable the KEEPALIVE option */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int recvbuffer = ROCK_TCP_RCVBUF;
    int sendbuffer = ROCK_TCP_SNDBUF;
    int keepalive = 1;
    int s_1 = setsockopt(activetcpsock, SOL_SOCKET, SO_RCVBUF, &recvbuffer, sizeof(recvbuffer));
    int s_2 = setsockopt(activetcpsock, SOL_SOCKET, SO_SNDBUF, &sendbuffer, sizeof(sendbuffer));
    int s_3 = setsockopt(activetcpsock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int s_4 = setsockopt(activetcpsock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int s_5 = setsockopt(activetcpsock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    if (s_1<0 || s_2<0 || s_3<0 || s_4<0 || s_5<0)
        printf("[server]\terror on setsockopt on tcp socket.\n");

    /* set SO_NOSIGPIPE option for the socket, ONLY ON OSX
        on Linux we already use the MSG_NOSIGNAL flag in the send call. */
#ifdef SO_NOSIGPIPE
        int nosigpipe = 1;
        int s_6 = setsockopt(activetcpsock, SOL_SOCKET, SO_NOSIGPIPE, (void *)&nosigpipe, sizeof(int));
        if (s_6 < 0)
            printf("[server]\terror on setsockopt SO_NOSIGPIPE on tcp socket.\n");
#endif

    /* save current active tcp socket, set the rocket to connected
     * and set tcp_task flag to 0 (aka task finished) */
    rocket->sd = activetcpsock;
    rocket->state = CONNECTED;
    rocket->tcp_task = 0;
    rocket->resetflag = 0;

    printf("[server]\ttcp socket on port %d is CONNECTED w/ rocket session cid %d.\n", port, rocket->cid);
    rocket_list_print_item(rocket);
    /* close the listening socket (not the active socket!) */
    int close_ret = close(tcpsock);
    if (close_ret < 0)
        printf("[server]\tfailed close on tcp socket file descriptor.\n");
    return NULL;
}

/* this thread should run 24/24h.
 * it starts the udp control socket and responds to requests
 *
 * PACKET TYPE  | SENDER    | DESCRIPTION
 * 1            | client    | request a new socket on a port
 * 2            | server    | start the DH key exchange routine
 * 3            | client    | finish the DH key exchange routine
 * 4            | server    | return the connection identifier (cid)
 * 5            | client    | start the reconnection routine
 * 6            | server    | authenticate the client w/ a challenge
 * 7            | client    | complete authentication w/ the response
 * 8            | server    | end of reconnection w/ last delivered byte
 * 200          | server    | generic success packet
 * 100          | server    | generic error packet
 * 50           | client    | heartbeat packet
 *
 * */
void *rocket_ctrl_listen(void *arg) {
    rocket_list_node **head = ((struct thread_arg *)arg)->head;
    pthread_mutex_t *lock = ((struct thread_arg *)arg)->lock;

    printf("[server]\tcontrol server started.\n");

    int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctrlsock < 0)
        printf("[server]\tctrl socket creation error.\n");
    /* set the SEND timeout (the RECV is by default 0) and the REUSEADDR option */
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
    struct sockaddr_in clientaddr;
    clientlen = sizeof(clientaddr);
    unsigned char ctrlbuf[ROCK_CTRLPKTSIZE];    /* we put here the received packet and the
                                                 * serialized one ready to send */
    /* start of the main loop: receive packet --> do things --> answer */
    while(1) {
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);       /* clean up the recv/send buffer */
        int recv_ret = recvfrom(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, &clientlen);
        if (recv_ret < ROCK_CTRLPKTSIZE) {
            printf("[server]\tctrl socket recv error.\n");
            continue;
        }
        /* initialize the packet object that we will receive
         * and the packet object we will send as response */
        rocket_ctrl_pkt *recvpkt = rocket_deserialize_ctrlpkt(ctrlbuf);
        rocket_ctrl_pkt *sendpkt = malloc(sizeof(rocket_ctrl_pkt));
        sendpkt->k = BN_new();

        /* request to create a new socket. it contains:
         * the tcp port and the client tcp receive buffer size */
        if (recvpkt->type == 1) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_findbyport(*head, recvpkt->port);
            pthread_mutex_unlock(lock);
            if (rocket == 0)            /* the server does not have a record for a rocket on this port */
                sendpkt->type = 100;
            else if (rocket->state == CONNECTED)    /* ops, the requested rocket is already connected */
                sendpkt->type = 100;
            else {
                /* initialize the inflight buffer with size given by the sum of
                 * the local tcp socket SEND buffer and the client RECV buffer */
                rocket->ifb = ifb_init(recvpkt->buffer + ROCK_TCP_SNDBUF);
                
                /* if the rocket is SUSPENDED and we receive a new rocket request
                 * from the client, there was probably a crash on it.
                 * reset every rocket counter on the server. */
                if (rocket->state == SUSPENDED) {
                    rocket->sentcounter = 0;
                    rocket->rcvdcounter = 0;
                    rocket->tosendbytes = 0;
                    rocket->resetflag = 1;
                    sleep(ROCK_SNDRCV_REFR+1); /* wait so every call can detect it */
                }

                /* start the Diffie-Hellman key exchange algorithm */
                BN_rand(rocket->a, ROCK_DH_BIT, 0, 0);                  /* generate a random private key a */
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
        /* client continues Diffie-Hellman key exchange algorithm.
         * the packet contains the tcp port and B (see DH) */
        else if (recvpkt->type == 3) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_findbyport(*head, recvpkt->port);
            pthread_mutex_unlock(lock);
            if (rocket == 0)
                sendpkt->type = 100;
            else if (rocket->state == CONNECTED || rocket->state == SUSPENDED)  /* ops, you should not be here! */
                sendpkt->type = 100;
            else {
                /* finish the Diffie-Hellman routine and store the shared private key k */
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
                sendpkt->buffer = ROCK_TCP_RCVBUF;      /* send to the client the server tcp recv buffer size */

                /* start the thread which will wait for the tcp connection */
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
        /* reconnection routine, the packet contains the cid of the rocket to reconnect */
        else if (recvpkt->type == 5) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_find(*head, recvpkt->cid);
            pthread_mutex_unlock(lock);
            if (rocket == 0 || (rocket!=0 && rocket->state == CLOSED))
                sendpkt->type = 100;
            else {
                /* receive from the client his received bytes counter, calculate how many
                bytes we need to resend making the difference with our sent bytes counter. */
                rocket->tosendbytes = rocket_mod_diff(rocket->sentcounter, recvpkt->buffer);

                sendpkt->type = 6;
                BN_rand(rocket->challenge, ROCK_DH_BIT, 0, 0);      /* random challenge to authenticate the client */
                sendpkt->k = BN_dup(rocket->challenge);             /* copy challenge inside packet to send */
            }

            bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
            rocket_serialize_ctrlpkt(sendpkt, ctrlbuf);
            int m = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
            if (m < ROCK_CTRLPKTSIZE) {
                printf("[server]\tctrl socket send error.\n");
                continue;
            }

        }
        /* finish the reconnection routine */
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
                    sendpkt->type = 8;
                    sendpkt->buffer = rocket->rcvdcounter;    /* send to the client our received bytes counter */

                    /* start the thread which will wait for the new tcp connection */
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
        /* heartbeat packet received, update rocket state */
        else if (recvpkt->type == 50) {
            pthread_mutex_lock(lock);
            rocket_t *rocket = rocket_list_find(*head, recvpkt->cid);
            pthread_mutex_unlock(lock);
            if (rocket == 0)
                sendpkt->type = 100;
            else {
                /* get the current time in seconds and update lasthbtime */
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

/* this thread should run 24/24h.
 * every ROCK_NET_CHECK seconds it checks last heartbeat timestamp
 * for every rocket. If it's passed more than ROCK_HB_RATE*ROCK_HB_MAXLOSS
 * the rocket state is set to SUSPENDED and the tcp socket is closed */
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
                        shutdown(node->rocket->sd, SHUT_RDWR);    /* shutdown socket and disable send & recv */
                        close(node->rocket->sd);    /* close current tcp socket */
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

/* start control server thread and network monitor thread */
int rocket_ctrl_server(rocket_list_node **head, pthread_mutex_t *lock) {
    pthread_t tid_listen, tid_network;
    struct thread_arg *arg = malloc(sizeof(struct thread_arg));
    arg->head = head;
    arg->lock = lock;
    pthread_create(&tid_listen, NULL, rocket_ctrl_listen, arg);
    pthread_create(&tid_network, NULL, rocket_network_monitor, arg);
                                        //TODO: remove joins. Leave it here just for debug purpose
    //pthread_join(tid_listen, NULL);
    //pthread_join(tid_network, NULL);
    return 0;
}

/* create a new rocket for the selected port and return the cid. 
 * leave it in the CLOSED state until a client will connect */
uint16_t rocket_server(rocket_list_node **head, uint16_t port, pthread_mutex_t *lock) {
    /* first of all, check if a rocket for that port already exists */
    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_findbyport(*head, port);
    pthread_mutex_unlock(lock);
    if (rocket != 0)
        return rocket->cid;
    //TODO: check if port is used by other apps
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
    rocket->sentcounter = 0;
    rocket->rcvdcounter = 0;
    rocket->tosendbytes = 0;
    rocket->resetflag = 0;
    /* generate a random connection identifier (cid) for the rocket
     * but first check if it already exists! :) */
    int cid_decided = 0;
    uint16_t cid = 0;
    srand(time(NULL));  /* seed the random generator */
    while (cid_decided == 0) {
        cid = (uint16_t)rand() % 65535;
        rocket_t *tmp = rocket_list_find(*head, cid);
        if (tmp == 0)
            cid_decided++;
    }
    rocket->cid = cid;

    pthread_mutex_lock(lock);
    rocket_list_insert(head, rocket, rocket->cid); /* insert the rocket into the list */
    pthread_mutex_unlock(lock);
    return cid;
}


/********************************************/
/************* CLIENT FUNCTIONS *************/
/********************************************/

/* this thread should run until the client is on.
 * it detects link availability sending heartbeats packets
 * every ROCK_HB_RATE seconds and eventually starts the reconnection */
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
        /* prepare the heartbeat packet */
        rocket_ctrl_pkt pkt_hb;
        pkt_hb.type = 50;
        pkt_hb.cid = cid;
        rocket_serialize_ctrlpkt(&pkt_hb, ctrlbuf);
        int send_50 = sendto(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (send_50 != ROCK_CTRLPKTSIZE) {
            /* this section should not be called... */
            //rocket->state = SUSPENDED;
            //printf("[client]\trocket %d is SUSPENDED.\n", rocket->cid);
        }
        bzero(ctrlbuf, ROCK_CTRLPKTSIZE);
        /* wait for an heartbeat response. After the recv timeout the heartbeat is missed */
        int recv_hbresp = recv(ctrlsock, ctrlbuf, ROCK_CTRLPKTSIZE, 0);
        /* get the current time in seconds */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        if (recv_hbresp < ROCK_CTRLPKTSIZE) {   /* no heartbeat response received before timeout, missed! */
            printf("[client]\trocket %d missed heartbeat.\n", rocket->cid);
            if (rocket->lasthbtime != 0 && 
                tv.tv_sec - rocket->lasthbtime > ROCK_HB_RATE * ROCK_HB_MAXLOSS) {
                rocket->state = SUSPENDED;
                shutdown(rocket->sd, SHUT_RDWR);    /* shutdown socket and disable send & recv */
                close(rocket->sd);  /* close the current tcp socket */

                printf("[client]\trocket %d is SUSPENDED.\n", rocket->cid);
            }
        }
        rocket_ctrl_pkt *pkt_hbresp = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_hbresp->type == 200 && rocket->state == SUSPENDED) {
            /* we can start the reconnection routine */
            int retry = ROCK_CTRLMAXRETRY;
            int routine = 1; /* select the reconnection routine */
            while(retry > 0) {
                int res = rocket_connect(routine, head, rocket->serveraddr, port, lock);
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

/* connection and reconnection routines */
int rocket_connect(int reconnect, rocket_list_node **head, char *addr, uint16_t port, pthread_mutex_t *lock) {
    /* connection routine */
    if (reconnect == 0) {
        int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctrlsock == -1)
            return -1;

        /* set tcp SEND and RECV buffer size */
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
        /* prepare the rocket request packet */
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
        BN_rand(b, ROCK_DH_BIT, 0, 0);              /* generate the random private key b */
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

        /* server is now ready to accept the tcp connection, start the connection */
        int tcpsock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpsock < 0)
            return -1;
        /* set the SEND and RECV buffer and TIMEOUT (to 0) and enable the KEEPALIVE option */
        struct timeval tvto;
        tvto.tv_sec = 0;
        tvto.tv_usec = 0;
        int recvbuffer = ROCK_TCP_RCVBUF;
        int sendbuffer = ROCK_TCP_SNDBUF;
        int keepalive = 1;
        int s_1 = setsockopt(tcpsock, SOL_SOCKET, SO_RCVBUF, &recvbuffer, sizeof(recvbuffer));
        int s_2 = setsockopt(tcpsock, SOL_SOCKET, SO_SNDBUF, &sendbuffer, sizeof(sendbuffer));
        int s_3 = setsockopt(tcpsock, SOL_SOCKET, SO_SNDTIMEO, &tvto, sizeof(tvto));
        int s_4 = setsockopt(tcpsock, SOL_SOCKET, SO_RCVTIMEO, &tvto, sizeof(tvto));
        int s_5 = setsockopt(tcpsock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        if (s_1<0 || s_2<0 || s_3<0 || s_4<0 || s_5<0)
            printf("[client]\terror on setsockopt on tcp socket.\n");
        
        /* set SO_NOSIGPIPE option for the socket, ONLY ON OSX
        on Linux we already use the MSG_NOSIGNAL flag in the send call. */
#ifdef SO_NOSIGPIPE
        int nosigpipe = 1;
        int s_6 = setsockopt(tcpsock, SOL_SOCKET, SO_NOSIGPIPE, (void *)&nosigpipe, sizeof(int));
        if (s_6 < 0)
            printf("[client]\terror on setsockopt SO_NOSIGPIPE on tcp socket.\n");
#endif

        struct sockaddr_in serveraddr_tcp;
        serveraddr_tcp.sin_family = AF_INET;
        serveraddr_tcp.sin_port = htons(port);
        inet_pton(AF_INET, addr, &serveraddr_tcp.sin_addr);
        int connect_ret = connect(tcpsock, (struct sockaddr *)&serveraddr_tcp, sizeof(serveraddr_tcp));
        if (connect_ret < 0)
            return -1;

        /* create the rocket record and set to CONNECTED state */
        rocket_t *rocket = malloc(sizeof(rocket_t));
        rocket->role = CLIENT;
        rocket->cid = cid;
        rocket->a = BN_new();
        rocket->k = k;
        rocket->state = CONNECTED;
        rocket->sd = tcpsock;
        rocket->port = port;
        rocket->ifb = ifb_init(buffer_size + ROCK_TCP_SNDBUF);
        rocket->lasthbtime = 0;
        rocket->serveraddr = addr;
        rocket->sentcounter = 0;
        rocket->rcvdcounter = 0;
        rocket->tosendbytes = 0;
        rocket->resetflag = 0;

        pthread_mutex_lock(lock);
        rocket_list_insert(head, rocket, cid);  /* insert it into the list */
        pthread_mutex_unlock(lock);

        /* start the client network monitor thread */
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
    /* reconnection routine */
    else if (reconnect == 1) {
        int ctrlsock = socket(AF_INET, SOCK_DGRAM, 0);
        if (ctrlsock == -1)
            return -1;

        /* set tcp SEND and RECV buffer size */
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
        /* prepare the rocket reconnection request packet */
        rocket_ctrl_pkt pkt_5;
        pkt_5.type = 5;
        pkt_5.cid = rocket->cid;
        pkt_5.buffer = rocket->rcvdcounter;   /* send to the server the received bytes counter */

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
            return ROCK_RESET;
        }
        /* complete the client authentication responding to the challenge */
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
        rocket_ctrl_pkt *pkt_8 = rocket_deserialize_ctrlpkt(ctrlbuf);
        if (pkt_8->type == 100)
            return -1;
        /* server sent his received bytes counter, so we can calculate
        how many bytes we need to resend to recovery the connection. */
        rocket->tosendbytes = rocket_mod_diff(rocket->sentcounter, pkt_8->buffer);

        /* we can now reconnect the tcp socket */
        int tcpsock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpsock < 0)
            return -1;
        /* set the SEND and RECV buffer and TIMEOUT (to 0) and enable the KEEPALIVE option */
        struct timeval tvto;
        tvto.tv_sec = 0;
        tvto.tv_usec = 0;
        int recvbuffer = ROCK_TCP_RCVBUF;
        int sendbuffer = ROCK_TCP_SNDBUF;
        int keepalive = 1;
        int s_1 = setsockopt(tcpsock, SOL_SOCKET, SO_RCVBUF, &recvbuffer, sizeof(recvbuffer));
        int s_2 = setsockopt(tcpsock, SOL_SOCKET, SO_SNDBUF, &sendbuffer, sizeof(sendbuffer));
        int s_3 = setsockopt(tcpsock, SOL_SOCKET, SO_SNDTIMEO, &tvto, sizeof(tvto));
        int s_4 = setsockopt(tcpsock, SOL_SOCKET, SO_RCVTIMEO, &tvto, sizeof(tvto));
        int s_5 = setsockopt(tcpsock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        if (s_1<0 || s_2<0 || s_3<0 || s_4<0 || s_5<0)
            printf("[client]\terror on setsockopt on tcp socket.\n");

        /* set SO_NOSIGPIPE option for the socket, ONLY ON OSX
        on Linux we already use the MSG_NOSIGNAL flag in the send call. */
#ifdef SO_NOSIGPIPE
        int nosigpipe = 1;
        int s_6 = setsockopt(tcpsock, SOL_SOCKET, SO_NOSIGPIPE, (void *)&nosigpipe, sizeof(int));
        if (s_6 < 0)
            printf("[client]\terror on setsockopt SO_NOSIGPIPE on tcp socket.\n");
#endif

        struct sockaddr_in serveraddr_tcp;
        serveraddr_tcp.sin_family = AF_INET;
        serveraddr_tcp.sin_port = htons(port);
        inet_pton(AF_INET, addr, &serveraddr_tcp.sin_addr);
        int connect_ret = connect(tcpsock, (struct sockaddr *)&serveraddr_tcp, sizeof(serveraddr_tcp));
        if (connect_ret < 0)
            return -1;

        /* set the rocket state to CONNECTED and save the socket file descriptor */
        rocket->state = CONNECTED;
        rocket->sd = tcpsock;

        return 0;
    }
    else
        return -1;
}

/* create a new rocket on the client and return the cid received from the server */
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
    return rocket->cid;
}


/********************************************/
/************* COMMON FUNCTIONS *************/
/********************************************/

/* remains blocked until the message pointed inside 'buffer'
 * of size 'length' is correctly sent. Returns < 0 if very
 * sad things happen. The length of the message is automatically
 * inserted in a 4 byte header sent before the message. 
 * Note that by 'correctly sent' we mean 'placed on a network-safe
 * outgoing queue' such that data will be delivered, sooner or later. */
int rocket_send(rocket_list_node **head, uint16_t cid, char *buffer, uint32_t length, pthread_mutex_t *lock) {
    int was_suspended = 0;

    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_find(*head, cid);
    pthread_mutex_unlock(lock);
    if (rocket == 0)
        return ROCK_ERR_NOTFOUND;
    
    /* add a 4 byte header with the length of the message */
    unsigned char *header = malloc(4);
    rocket_ltobytes(length, header);

    /* IMPORTANT!! here we set the flags for the send call.
    flag MSG_NOSIGNAL (since Linux 2.2):
    Requests not to send SIGPIPE on errors on stream oriented sockets 
    when the other end breaks the connection. The EPIPE error is still returned. 
    THIS FLAG IS NOT SUPPORTED ON OSX, so on that we use instead the SO_NOSIGPIPE
    option with the setsockopt command. */
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif

    /* first send the 4 byte header */
    int sentheaderbytes = 0;
    while (sentheaderbytes != 4) {
        if (rocket->resetflag == 1)
            return ROCK_RESET;
        if (rocket->state == SUSPENDED || rocket->state == CLOSED) {
            was_suspended = 1;
            sleep(ROCK_SNDRCV_REFR);
        }
        else {
            if (was_suspended == 1) {
                was_suspended = 0;
                /* re-send last rocket->tosendbytes bytes from the in-flight buffer
                 * without updating the rocket->sentcounter and rocket->tosendbytes */
                int recoverybytessent = 0;

                while (recoverybytessent < rocket->tosendbytes) {
                    int rs = send(rocket->sd, ifb_getlastpushed(rocket->ifb, rocket->tosendbytes) + recoverybytessent, rocket->tosendbytes - recoverybytessent, flags);
                    if (rs > 0) {
                        recoverybytessent += rs;
                    }
                    else {
                        was_suspended = 1;
                        sleep(ROCK_SNDRCV_REFR);
                        break;
                    }
                }
                if (recoverybytessent == rocket->tosendbytes)
                    printf("[data]\t\tre-sent last %d bytes to recovery connection.\n", rocket->tosendbytes);
                else
                    continue;
            }
            int h = send(rocket->sd, header + sentheaderbytes, 4 - sentheaderbytes, flags);
            if (h < 0) {
                printf("[data]\t\ttcp send returned -1 while sending the header: \
                        indicating a closed socket or a network failure.\n");
            }
            else {
                /* update the sent bytes counter and store in a uint32_t variable */
                rocket->sentcounter = (rocket->sentcounter + h) % UINT32_MAX;
                /* push those sent bytes inside the inflight buffer */
                ifb_push(rocket->ifb, (char *)header + sentheaderbytes, h);
                sentheaderbytes += h;
            }
        }
    }

    /* now send the message of length defined in the header */
    int sentbytes = 0;
    while (sentbytes < length) {
        if (rocket->resetflag == 1)
            return ROCK_RESET;
        if (rocket->state == SUSPENDED || rocket->state == CLOSED) {
            was_suspended = 1;
            sleep(ROCK_SNDRCV_REFR);
        }
        else {
            if (was_suspended == 1) {               /* rocket resumed after a suspension */
                was_suspended = 0;
                
                /* re-send last rocket->tosendbytes bytes from the in-flight buffer
                 * without updating the rocket->sentcounter and rocket->tosendbytes */
                int recoverybytessent = 0;

                while (recoverybytessent < rocket->tosendbytes) {
                    if (rocket->resetflag == 1)
                        return ROCK_RESET;
                    int rs = send(rocket->sd, ifb_getlastpushed(rocket->ifb, rocket->tosendbytes) + recoverybytessent, rocket->tosendbytes - recoverybytessent, flags);
                    if (rs > 0) {
                        recoverybytessent += rs;
                    }
                    else {
                        was_suspended = 1;
                        sleep(ROCK_SNDRCV_REFR);
                        break;
                    }
                }
                if (recoverybytessent == rocket->tosendbytes)
                    printf("[data]\t\tre-sent last %d bytes to recovery connection.\n", rocket->tosendbytes);
                else
                    continue; 
            }
            int s = send(rocket->sd, buffer + sentbytes, length - sentbytes, flags);
            if (s < 0) {
                //printf("[data]\ttcp send returned -1: indicating a closed socket or a network failure.\n");
            }
            else {
                /* update the sent bytes counter and store in a uint32_t variable */
                rocket->sentcounter = (rocket->sentcounter + s) % UINT32_MAX;
                /* push those sent bytes inside the inflight buffer */
                ifb_push(rocket->ifb, buffer + sentbytes, s);
                sentbytes += s;
            }
        }
    }
    printf("[data]\t\trocket_send successfully completed: %d bytes sent.\n", sentbytes);
    if (sentbytes > length)
        printf("[data]\t\tWARNING! sent %d bytes instead of %d bytes.\n", sentbytes, length);
    free(header);
    return sentbytes;
}

/* remains blocked until a message is correctly received.
 * it reads the length of the message contained in the 4
 * byte header and allocate the space for the message.
 * the received message will be pointed by buffer, and
 * the function will return the length of the message
 * (only the message, without the header).
 * Can return < 0 if sad things happen. */
int rocket_recv(rocket_list_node **head, uint16_t cid, char **buffer, pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_find(*head, cid);
    pthread_mutex_unlock(lock);
    if (rocket == 0)
        return ROCK_ERR_NOTFOUND;

    /* receive the first 4 bytes which contains the msg length */
    unsigned char *header = malloc(4);
    int rcvdheaderbytes = 0;
    while (rcvdheaderbytes != 4) {
        if (rocket->resetflag == 1)
            return ROCK_RESET;
        if (rocket->state == SUSPENDED || rocket->state == CLOSED) {
            sleep(ROCK_SNDRCV_REFR);
        }
        else {
            int h = recv(rocket->sd, header + rcvdheaderbytes, 4 - rcvdheaderbytes, 0);
            if (h < 0) {
                printf("[data]\t\ttcp recv returned -1 while receiving the header: \
                        indicating a closed socket or a network failure.\n");
            }
            else {
                rcvdheaderbytes += h;
                /* update the received bytes counter and store in a uint32_t variable */
                rocket->rcvdcounter = (rocket->rcvdcounter + h) % UINT32_MAX;
            }
        }
    }
    uint32_t length = rocket_bytestol(header);
    *buffer = malloc(length);        /* allocate memory space for the message content */

    /* now receive the message of length indicated in the header */
    int rcvdbytes = 0;
    while (rcvdbytes < length) {
        if (rocket->resetflag == 1)
            return ROCK_RESET;
        if (rocket->state == SUSPENDED || rocket->state == CLOSED) {
            sleep(ROCK_SNDRCV_REFR);
        }
        else {
            int r = recv(rocket->sd, *buffer + rcvdbytes, length - rcvdbytes, 0);
            if (r < 0) {
                //printf("[data]\t\ttcp recv returned -1: indicating a closed socket or a network failure.\n");
            }
            else {
                rcvdbytes += r;
                /* update the received bytes counter and store in a uint32_t variable */
                rocket->rcvdcounter = (rocket->rcvdcounter + r) % UINT32_MAX;
            }
        }
    }
    printf("[data]\t\trocket_recv successfully completed: %d bytes received.\n", rcvdbytes);
    if (rcvdbytes > length)
        printf("[data]\t\tWARNING! received %d bytes instead of %d bytes.\n", rcvdbytes, length);
    free(header);
    return rcvdbytes;
}

/* close the socket and free every related memory allocation */
int rocket_close(rocket_list_node **head, uint16_t cid, pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
    rocket_t *rocket = rocket_list_find(*head, cid);
    pthread_mutex_unlock(lock);
    if (rocket == 0)
        return -1;

    BN_free(rocket->a);
    BN_free(rocket->k);
    BN_free(rocket->challenge);
    ifb_free(rocket->ifb);
    shutdown(rocket->sd, SHUT_RDWR);
    close(rocket->sd);
    pthread_cancel(rocket->cnet_monitor);

    pthread_mutex_lock(lock);
    if (rocket_list_remove(head, cid) < 0)
        return -1;
    pthread_mutex_unlock(lock);
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc > 3 && strcmp(argv[1], "-c")==0) {
        printf("--client mode--\n");
        pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(lock, NULL);
        rocket_list_node *head = 0;
        uint16_t cid = rocket_client(&head, argv[2], (uint16_t)atoi(argv[3]), lock);
        rocket_list_print(head);
        
        rocket_send(&head, cid, "abcabcabcabca", 13, lock);
        sleep(1800); //just for debug sleep for 30min
    }
    else if (argc > 1 && strcmp(argv[1], "-s")==0) {
        printf("--server mode--\n");
        pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(lock, NULL);
        rocket_list_node *head = 0;
        uint16_t cid = rocket_server(&head, 125, lock);
        rocket_ctrl_server(&head, lock);
        
        while (1) {
            char *buffer;
            int length = rocket_recv(&head, cid, &buffer, lock);
        }
        sleep(1800); //just for debug sleep for 30min
    } 
    else
        printf("usage: rocket [-c server_ip_address tcp_port] [-s]\n");

	return 0;
}