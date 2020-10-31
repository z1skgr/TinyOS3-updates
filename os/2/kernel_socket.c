#include "kernel_socket.h"
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_threads.h"


/*
*	Ola ta threads dhmiourgountai ws unbound kai mono ean tous eixe dw8ei tote port mporoun na ginoun listeners.
*	Otan ena socket 8elei na kanei connect dhmiourgei ena struct, wste na uparxei sunxronismos metaksu tou server kai tou client.
*	Afou to dhmiourghsei perimenei mexri na tou epistrafei me to fcb tou antigrfou tou server, opou 8a ginei h sindesh peer to peer.
*	H Shutdown kobei thn epikinwnia kleinwntas ta pipes. Gia na diagraftoun ta sockets prepei na kalestei h close.
*	H close kanei diaforetikh diadikasia analoga to eidos tou socket.
*	Peripou to 2-3 % twn sundesewn apotixanei, pisteuoume logo ths thread_join. Sto validate_api fainetai ws Test timed out.
*/


/******************** Socket ops *********************/

// Oi read kai oi write aplws kaloun tis katalliles pipe_read kai pipe_write.
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

		// An kalestei h close se ena antigrafo tou server prepei na diagraftei.
		if(socket->peer != NULL)
			free(socket->peer);

		free(socket);
	}
	else if(socket->type == PEER){

		// Close connection and free socket.

		/* Ta pipes kleinoun otan teleiwsoun ola ta nimata. Mporoume na ta xrhsimopoihsoume
		 * gia na kseroume an exoun teleiwsei ta nhmata tou peer socket. */
		pipe_CB* pipes[2] = {(pipe_CB*) socket->peer->pipes_FCBs[0]->streamobj, (pipe_CB*) socket->peer->pipes_FCBs[1]->streamobj};
		socket->peer->pipes_FCBs[0]->streamfunc->Close(socket->peer->pipes_FCBs[0]->streamobj);
		socket->peer->pipes_FCBs[1]->streamfunc->Close(socket->peer->pipes_FCBs[1]->streamobj);

		/* exoun kleisei kai ta 2 pipe kai apo tis 2 meries.
		 * exei kleisei to ena pipe apo ayth thn meria kai to allo kai apo tis dyo meries.
		 *  -||-
		 * exoun kleisei kai ta 2 pipe mono apo authn th meria.	*/
		if( (pipes[0] == NULL && pipes[1] == NULL)
			|| (pipes[0] == NULL && pipes[1]->writer_closed == 1)
			|| (pipes[0]->reader_closed == 1 && pipes[1] == NULL)
			|| (pipes[0]->reader_closed == 1 && pipes[1]->writer_closed == 1)){

			free(socket->peer);
			free(socket);
		}
	}
	else{// listener
	
		// Ekleise to nhma ara ksipname ta upoloipa kai tous enimerwnoume ti egine.
		socket->listener->closing = 1;
		Cond_Broadcast(& socket->listener->server_cv);

		//Ksipnima clients.
		while(!is_rlist_empty(& socket->listener->request_list)){
			rlnode* sel = rlist_pop_front(& socket->listener->request_list);
			Cond_Signal(& sel->request->client_cv);
		}

		// eleu8erwsh port 
		PORT_MAP[socket->portNum] = NULL;

		// Perimene na adiasoun ta cv. Bzoume sched_mutex gia na dwsei proteraiwthta sta alla.
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

	// Allocate gia to unbound an xreiazetai

	// Init socket.
	socket->type = UNBOUND;
	socket->portNum = port;
	
	// Reserve FCB
	Fid_t socket_fid = 0;
	socket->fcb = NULL;

	// elengxos gia to reserve
	if(!FCB_reserve(1, &socket_fid, &(socket->fcb))){
		free(socket);
		return -1;
	} 

  	// Gemisma fcb.
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

	// Den ginetai ena socket xwris port na ginei listener h na ginei se ena piasmeno port.
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

	// De 8eloume na kanei kaneis join sto thread pou eftiakse ton listener.
	sys_ThreadDetach(sys_ThreadSelf());

	// Success.
	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{

	/***************** Elenxos socket ************************/

	// Get fcb. Check legality and if it has a socket. Then get it.
	FCB* fcb = get_fcb(lsock);
	if (fcb == NULL || fcb->streamfunc != &socket_ops)
		return NOFILE;
	SCB* lsocket = (SCB*) fcb->streamobj;

	// Check if socket is a listener.
	if(lsocket->type != LISTENER)
		return NOFILE;

	/****************** Anamonh sundeshs ***********************/

	lsocket->listener->server_thread_count++;

	// Ta nhmata tou server prepei na koimi8oun an den exoun na eksipirethsoun kanena connect. Ksipnaei ena otan emfanistei request.
	while(is_rlist_empty(& lsocket->listener->request_list)){
		kernel_wait(& lsocket->listener->server_cv,SCHED_PIPE);

		// While waiting, the listening socket @c lsock was closed.
		if(lsocket->listener->closing){
			lsocket->listener->server_thread_count--;

			// To teleutaio nhma pou 8a figei logo close tha ksipnisei auto pou diagrafei ton listener.
			if(lsocket->listener->server_thread_count == 0)
				Cond_Broadcast(& lsocket->listener->close_cv);
			return NOFILE;
		}
	}

	lsocket->listener->server_thread_count--;

	/******************	Dhmiourgia antigrafou	*******************/

	// Reserve FCB
	Fid_t socket_fid = 0;
	FCB* new_fcb = NULL;

	// elengxos gia to reserve. Afou den eksipirethse to request prepei na to ksaanebasei.
	if(!FCB_reserve(1, &socket_fid, &new_fcb)){
		return NOFILE;
	}

	// Pernei ena request.
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

	// Bazoume sto request to antigrafo tou listener.
	request->server_copy_fcb = new_fcb;

	// ksipname ton client pou ekane to request.
	Cond_Signal(& request->client_cv);

	return socket_fid;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{

	/***************** Elenxos socket kai port. ************************/

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

	/****************** Anamonh sundeshs ***********************/

	// Dhmiourgia request
	request_t* request = (request_t*) xmalloc(sizeof(request_t));
	request->client_cv = COND_INIT;
	request->server_copy_fcb = NULL;

	//Apostolh request.
	rlist_push_front(& PORT_MAP[port]->listener->request_list, rlnode_init(&(request->node), request));

	// Enhmerwsh tou server oti zhtaei o client eksipiretish.
	Cond_Signal(& PORT_MAP[port]->listener->server_cv);

	// Oso den uparxoun antigrafa tou server gia na eksipirethsoun ton client, autos perimenei.
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

	/******************	Dhmiourgia Sundeshs	*******************/

	// Pernei to socket pou eftiakse o server.
	SCB* socket_server = (SCB*) request->server_copy_fcb->streamobj;
	socket_server->type = PEER;

	// Afou o client parei to antigrafo tou server, to request einai axrhsto kai diagrafetai.
	rlist_remove(& request->node);
	free(request);

	//	socket_client metatroph se peer
	socket_client->peer = (peer_t*) xmalloc(sizeof(peer_t));
	socket_client->type = PEER;

	// dhmiourgia pipes kai twn fcbs tous.
	socket_client->peer->pipes_FCBs[0] = (FCB*) xmalloc(sizeof(FCB));	//reader client
	socket_client->peer->pipes_FCBs[1] = (FCB*) xmalloc(sizeof(FCB));	//writer client

	socket_server->peer->pipes_FCBs[0] = (FCB*) xmalloc(sizeof(FCB));	//reader server
	socket_server->peer->pipes_FCBs[1] = (FCB*) xmalloc(sizeof(FCB));	//writer server

	FCB* temp_fcb_array[2];
	
	// Dhmiourgia pipes kai sundesh tous.
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
	/***************** Elenxos socket ************************/

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