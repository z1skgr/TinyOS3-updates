#include "kernel_socket.h"
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_threads.h"


/*
* All threads are created as unbound and only if they were given a port at that time can they become listeners
* When a socket wants to connect it creates a struct, so that there is synchronization between server and client
* After it creates it, it waits until it is returned with the fcb of the server copy where the peer to peer connection will be made
* Shutdown cuts the communication by closing the pipes. To delete the sockets, close must be called
* close does different things depending on the type of socket
* About 2-3% of connections fail because of thread_join. In validate_api it shows up as test timed out

*/


/******************** Socket ops *********************/

// The read and write simply call the appropriate pipe_read and pipe_write.
int socket_write(void* socket_obj, const char* buf, unsigned int size)
{
	SCB* socket = (SCB*) socket_obj;

	int retcode = -1;
	int (*devwrite)(void*, const char*, uint) = NULL;
	void* sobj = NULL;

  /* Get the fields from the stream */
	FCB* fcb = socket->peer->pipes_FCBs[1];

	if(fcb) {

		sobj = fcb->streamobj;
		devwrite = fcb->streamfunc->Write;

		if(devwrite)
			retcode = devwrite(sobj, buf, size);
	}
	return retcode;
}


int socket_read(void* socket_obj, char* buf, unsigned int size)
{
	SCB* socket = (SCB*) socket_obj;

	int retcode = -1;
	int (*devread)(void*, char*, uint) = NULL;
	void* sobj = NULL;

	/* Get the fields from the stream */
	FCB* fcb = socket->peer->pipes_FCBs[0];

	if(fcb) {
		sobj = fcb->streamobj;
		devread = fcb->streamfunc->Read;

		if(devread)
			retcode = devread(sobj, buf, size);
	}
	return retcode;
}


int socket_close(void* socket_obj)
{
	SCB* socket = (SCB*) socket_obj;

	if(socket->type == UNBOUND){

		// If close is called a copy of the server must be deleted.
		if(socket->peer != NULL)
			free(socket->peer);

		free(socket);
	}
	else if(socket->type == PEER){

		// Close connection and free socket.

		/* The pipes are closed when all the threads are finished. We can 
		 * use them to know if the threads of the peer socket are finished. */
		pipe_CB* pipes[2] = {(pipe_CB*) socket->peer->pipes_FCBs[0]->streamobj, (pipe_CB*) socket->peer->pipes_FCBs[1]->streamobj};
		socket->peer->pipes_FCBs[0]->streamfunc->Close(socket->peer->pipes_FCBs[0]->streamobj);
		socket->peer->pipes_FCBs[1]->streamfunc->Close(socket->peer->pipes_FCBs[1]->streamobj);

		/* Closed both the 2 pipes from both the 2 sides.
		 * It has closed one pipe from side and the other from both sides.
		 *  -||-
		 * Both 2 pipes have been closed from this section only.	*/
		if( (pipes[0] == NULL && pipes[1] == NULL)
			|| (pipes[0] == NULL && pipes[1]->writer_closed == 1)
			|| (pipes[0]->reader_closed == 1 && pipes[1] == NULL)
			|| (pipes[0]->reader_closed == 1 && pipes[1]->writer_closed == 1)){

			free(socket->peer);
			free(socket);
		}
	}
	else{// listener
	
		// Closed the thread, so we woke up the rest of them and let them know what happened.
		socket->listener->closing = 1;
		Cond_Broadcast(& socket->listener->server_cv);

		//Wake clients.
		while(!is_rlist_empty(& socket->listener->request_list)){
			rlnode* sel = rlist_pop_front(& socket->listener->request_list);
			Cond_Signal(& sel->request->client_cv);
		}

		// Free port 
		PORT_MAP[socket->portNum] = NULL;

		// Waiting for the cv. sched_mutex to empty to give priority to the others
		while(socket->listener->server_thread_count > 0){
			kernel_wait(& socket->listener->close_cv, SCHED_MUTEX);
		}

		//free listener kai socket
		free(socket->listener);
		free(socket);	
	}
	return 0;
}

static file_ops socket_ops = {
  .Open = NULL,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close
};


/********************	System calls.	********************/


Fid_t sys_Socket(port_t port)
{	
	// Port out of bounds or taken.
	if (port < 0 || port > MAX_PORT || PORT_MAP[port] != NULL)
		return -1;

	// Allocate space for socket.
	SCB* socket = (SCB*) xmalloc(sizeof(SCB));

	// Allocate for unbound if needed

	// Init socket.
	socket->type = UNBOUND;
	socket->portNum = port;
	
	// Reserve FCB
	Fid_t socket_fid = 0;
	socket->fcb = NULL;

	// Condition for the reserve
	if(!FCB_reserve(1, &socket_fid, &(socket->fcb))){
		free(socket);
		return -1;
	} 

  	// Fill fcb.
  	socket->fcb->streamobj = socket;
  	socket->fcb->streamfunc = &socket_ops;

	return socket_fid;
}


int sys_Listen(Fid_t sock)
{
	// Get fcb. Check legality and if it has a socket. Then get it.
	FCB* fcb = get_fcb(sock);
	if (fcb == NULL || fcb->streamfunc != &socket_ops)
		return -1;
	SCB* socket = (SCB*) fcb->streamobj;

	// A socket without a port cannot become a listener or become a listener on a busy port.
	if (socket->portNum == NOPORT || PORT_MAP[socket->portNum] != NULL)
		return -1;

	// Socket already init.
	if (socket->type != UNBOUND)
		return -1;

	// Make socket a listener.
	socket->type = LISTENER;

	// Acquire space.
	socket->listener = (listener_t*) xmalloc(sizeof(listener_t));

	// Init.
	socket->listener->server_cv = COND_INIT;
	socket->listener->close_cv = COND_INIT; //Koimatai o listener mexri na adeiasei to cv tou

	socket->listener->server_thread_count = 0;

	rlnode_init(&(socket->listener->request_list), NULL);

	socket->listener->closing = 0;

	// Acquire port.
	PORT_MAP[socket->portNum] = socket;

	// Forbit join the thread that created the listener.
	sys_ThreadDetach(sys_ThreadSelf());

	// Success.
	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{

	/***************** Control socket ********************************/

	// Get fcb. Check legality and if it has a socket. Then get it.
	FCB* fcb = get_fcb(lsock);
	if (fcb == NULL || fcb->streamfunc != &socket_ops)
		return NOFILE;
	SCB* lsocket = (SCB*) fcb->streamobj;

	// Check if socket is a listener.
	if(lsocket->type != LISTENER)
		return NOFILE;

	/****************** Waiting for connection ***********************/

	lsocket->listener->server_thread_count++;

	// The server threads should sleep if they don't have any connections to serve. It wakes one up when a request appears
	while(is_rlist_empty(& lsocket->listener->request_list)){
		kernel_wait(& lsocket->listener->server_cv,SCHED_PIPE);

		// While waiting, the listening socket @c lsock was closed.
		if(lsocket->listener->closing){
			lsocket->listener->server_thread_count--;

			// The last thread to leave due to close will wake up the one that will delete the listener
			if(lsocket->listener->server_thread_count == 0)
				Cond_Broadcast(& lsocket->listener->close_cv);
			return NOFILE;
		}
	}

	lsocket->listener->server_thread_count--;

	/******************	Make copy	*******************/

	// Reserve FCB
	Fid_t socket_fid = 0;
	FCB* new_fcb = NULL;

	// Since it did not serve the request, it has to read it again.
	if(!FCB_reserve(1, &socket_fid, &new_fcb)){
		return NOFILE;
	}

	// Takes a request.
	rlnode* sel = rlist_pop_front(& lsocket->listener->request_list);
	request_t* request = sel->request;

	new_fcb->streamfunc = lsocket->fcb->streamfunc;

	// Allocate space for socket.
	SCB* new_socket = (SCB*) xmalloc(sizeof(SCB));
	new_socket->peer = (peer_t*) xmalloc(sizeof(peer_t));

	new_socket->fcb = new_fcb;
	new_fcb->streamobj = new_socket;

	// Init as peer.
	new_socket->type = UNBOUND;
	new_socket->portNum = lsocket->portNum;

	new_socket->peer->port_accepted = 0;

	// We put in the request the copy of the listener.
	request->server_copy_fcb = new_fcb;

	// Wake up client that made the request.
	Cond_Signal(& request->client_cv);

	return socket_fid;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{

	/***************** Control socket and port. ************************/

	// Get fcb. Check legality and if it has a socket. Then get it.
	FCB* fcb = get_fcb(sock);
	if (fcb == NULL || fcb->streamfunc != &socket_ops)
		return -1;
	SCB* socket_client = (SCB*) fcb->streamobj;

	// Check if socket is unbound.
	if(socket_client->type != UNBOUND)
		return -1;

	// Port out of bounds or empty.
	if (port < 0 || port > MAX_PORT || PORT_MAP[port] == NULL)
		return -1;

	// No timeout.
	if(timeout <= 0)
		timeout = NO_TIMEOUT;

	// A timeout of at least 500 msec is reasonable.
	if(timeout != NO_TIMEOUT && timeout < 500)
		timeout = 500;

	/****************** Waiting for connection ***********************/

	// Make request
	request_t* request = (request_t*) xmalloc(sizeof(request_t));
	request->client_cv = COND_INIT;
	request->server_copy_fcb = NULL;

	//Send request.
	rlist_push_front(& PORT_MAP[port]->listener->request_list, rlnode_init(&(request->node), request));

	// Informing the server that the client is requesting service.
	Cond_Signal(& PORT_MAP[port]->listener->server_cv);

	// As long as there are no copies of the server to serve the client, the client waits.
	while(request->server_copy_fcb == NULL)
	{
		if(!kernel_timedwait(& request->client_cv, SCHED_PIPE, timeout)){
			rlist_remove(& request->node);
			free(request);
			return -1;
		}
		
		// while waiting, the listening socket was closed.
		if(PORT_MAP[port]->listener->closing){			
			free(request);
			return -1;		
		}
	}

	/******************	Establish connection	*******************/

	// Passes the socket created by the server.
	SCB* socket_server = (SCB*) request->server_copy_fcb->streamobj;
	socket_server->type = PEER;

	// After the client gets the server copy, the request is useless and deleted.
	rlist_remove(& request->node);
	free(request);

	//	socket_client convert to peer
	socket_client->peer = (peer_t*) xmalloc(sizeof(peer_t));
	socket_client->type = PEER;

	// Make pipes their fcbs.
	socket_client->peer->pipes_FCBs[0] = (FCB*) xmalloc(sizeof(FCB));	//reader client
	socket_client->peer->pipes_FCBs[1] = (FCB*) xmalloc(sizeof(FCB));	//writer client

	socket_server->peer->pipes_FCBs[0] = (FCB*) xmalloc(sizeof(FCB));	//reader server
	socket_server->peer->pipes_FCBs[1] = (FCB*) xmalloc(sizeof(FCB));	//writer server

	FCB* temp_fcb_array[2];
	
	// Make pipes and establish their connections.
	temp_fcb_array[0] = socket_client->peer->pipes_FCBs[0];
	temp_fcb_array[1] = socket_server->peer->pipes_FCBs[1];
	create_pipe(temp_fcb_array);

	temp_fcb_array[0] = socket_server->peer->pipes_FCBs[0];
	temp_fcb_array[1] = socket_client->peer->pipes_FCBs[1];
	create_pipe(temp_fcb_array);

	socket_client->peer->port_accepted = 1;
	socket_server->peer->port_accepted = 1;

	return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	/***************** Control socket  ************************/

	int retcode = -1;

	// Get fcb. Check legality and if it has a socket. Then get it.
	FCB* fcb = get_fcb(sock);
	if (fcb == NULL || fcb->streamfunc != &socket_ops)
		return retcode;
	SCB* socket = (SCB*) fcb->streamobj;

	// Check if socket is peer and connected.
	if(socket->type != PEER || !socket->peer->port_accepted)
		return retcode;

	// Illegal shutdown mode.
	if(how < 1 || how > 3)
		return retcode;

	/***************** Shutdown pipes ************************/

	FCB* pipe_fcb[2] = {socket->peer->pipes_FCBs[0], socket->peer->pipes_FCBs[1]}; 

	if(how == SHUTDOWN_READ)
		retcode = pipe_fcb[0]->streamfunc->Close(pipe_fcb[0]->streamobj);
	else if(how == SHUTDOWN_WRITE)
		retcode = pipe_fcb[1]->streamfunc->Close(pipe_fcb[1]->streamobj);
	else{
		retcode = pipe_fcb[0]->streamfunc->Close(pipe_fcb[0]->streamobj);
		retcode =+ pipe_fcb[1]->streamfunc->Close(pipe_fcb[1]->streamobj);
	}
	return retcode;
}