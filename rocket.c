#include "rocket.h"

#include <string.h>
#include <sys/socket.h>

void *rocket_ctrl_listen(void *arg) {
    rocket_list_node **head = (rocket_list_node **)arg;
    while(1) {
        /*
         *  msg = recv(...)
         *  if (msg == ...) do... send...
         *  else if (msg == ...) do... send...
         *  else if (msg == ...) do... send...
         * */
    }
    return NULL;
}

void *rocket_network_monitor(void *arg) {
    rocket_list_node **head = (rocket_list_node **)arg;
    /* monitor heartbeats and do things */
    return NULL;
}

int rocket_ctrl_server(rocket_list_node **head) {
    pthread_t tid_listen, tid_network;
    pthread_create(&tid_listen, NULL, rocket_ctrl_listen, head);
    pthread_create(&tid_network, NULL, rocket_network_monitor, head);
    pthread_join(tid_listen, NULL);
    pthread_join(tid_network, NULL);
    return 0;
}

int main(int argc, char *argv[]) {
	if (argc > 1) {
		strcmp(argv[1], "-c")==0 ? printf("client mode\n") : 0;
		strcmp(argv[1], "-s")==0 ? printf("server mode\n") : 0;
	}

    

	return 0;
}
