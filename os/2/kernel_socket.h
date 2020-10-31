#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "tinyos.h"
#include "util.h"
#include "kernel_streams.h"



typedef enum{
	UNBOUND,
	PEER,
	LISTENER
}socket_type;


typedef struct listener_s
{
	//	Metrane posa threads einai mesa sta cv, wste na prostateutoun kata to exit tou listener.
	CondVar server_cv;
	int server_thread_count;

	// Lista me ta request.
	rlnode request_list;

	// Xrhsimopoiountai kata to klisimo. Ksipname ta threads pou einai sto accept kai meta eleu8erwnoume tis domes.
	CondVar close_cv;
	int closing;

} listener_t;


typedef struct peer_s
{
	int port_accepted;
	FCB* pipes_FCBs[2];

} peer_t;


typedef struct socket_control_block
{
	socket_type type;
	FCB* fcb;
	port_t portNum;

	// Ta socket xreiazontai diaforetikes plirofories analoga an einai listener h peer.
	union{
		listener_t* listener;
		peer_t* peer;
	};

} SCB;


//	Request struct. To dhmiourgei o client wste na tou dwsei o server ena antigrafo tou.
typedef struct request_struct
{
	rlnode node;
	CondVar client_cv;
	FCB* server_copy_fcb;

}request_t;

/***********************************

  The port table

***********************************/

SCB* PORT_MAP[MAX_PORT + 1];


/************** Socket operations *****************/ 


int socket_write(void* socket_obj, const char* buf, unsigned int size);


int socket_read(void* socket_obj, char* buf, unsigned int size);


int socket_close(void* socket_obj);


#endif