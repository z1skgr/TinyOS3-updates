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
	//	Count how many threads in cvs, for their protection during listener exit.
	CondVar server_cv;
	int server_thread_count;

	// List for requests.
	rlnode request_list;

	// Used at closing. We wake up the threads that are on accept and then release the structures
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

	// Sockets need different information depending on whether they are listener or peer.
	union{
		listener_t* listener;
		peer_t* peer;
	};

} SCB;


//	Request struct. The client creates it so that the server can give him a copy.
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