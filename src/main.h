#ifndef MAIN_H
#define MAIN_H

typedef void (*freemoted_state_proc) (int,void*);

#define FREEMOTED_STATUS_STARTING 0
#define FREEMOTED_STATUS_LISTENING 1
#define FREEMOTED_STATUS_FAILED 2
#define FREEMOTED_STATUS_STOPPING 3
#define FREEMOTED_STATUS_STOPPED 4

#define FREEMOTED_FAILURE_READCONFIG 1
#define FREEMOTED_FAILURE_BIND 2
#define FREEMOTED_FAILURE_BIND_BT 3

// Inbound connection types
#define CT_BLUETOOTH 1
#define CT_SOCKET 2

void f_startup(void *arg);
void f_shutdown();
void f_setAcceptPort(int port);
void f_setConfigFile(char* file);
int f_count_connections();
void f_setConnectionType(int type);

#endif

