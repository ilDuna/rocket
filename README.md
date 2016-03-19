# Rocket
*A Robust Socket (rocket) implementation based on Reliable Network Connections [Victor C. Zandy, Barton P. Miller] (http://goo.gl/AwuFvG) and Robust TCP Connections for Fault Tolerant Computing [Richard Ekwall, Péter Urbán, André Schiper] (http://goo.gl/cB4APH)*

To compile with flags **-lcrypto -pthread**.

### Server
```c
pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
pthread_mutex_init(lock, NULL);
rocket_list_node *head = 0;
uint16_t cid = rocket_server(&head, 120, lock);
rocket_ctrl_server(&head, lock);
char *buffer;
rocket_recv(&head, cid, &buffer, lock);
printf("received %s\n", buffer);
```

### Client
```c
pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
pthread_mutex_init(lock, NULL);
rocket_list_node *head = 0;
uint16_t cid = rocket_client(&head, 127.0.0.1, 120, lock);
rocket_list_print(head);    /* print the connected rocket */
rocket_send(&head, cid, "abcabcabcabca", 13, lock);
```
