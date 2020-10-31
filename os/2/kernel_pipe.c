#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_cc.h"



int pipe_read(void* pipe_obj, char *buf, unsigned int size)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;
	unsigned int count = 0;

	if(pipe->reader_closed == 1) return -1;

  	for(; count < size; count++) {

  		/*	an o buffer einai adios kai den iparxei kaneis na grapsei, prepei na epistrepsei me
  		 	osa stoixeia katafere na diavasei. Otan 8a exei ftasei sto EOF tha epistrefei 0. */
  		if(pipe->bufferElementsCount == 0 && pipe->writer_closed == 1)
  			return count;

  		pipe->ref_count_reader++;

  		/*	An o buffer einai adios alla uparxei kapios na grapsei, kimizoume to read.
  			Ksipnaei to write epeidh apeleu8erw8hke xwros h o buffer einai adeios.	*/
  		while(pipe->bufferElementsCount == 0 && pipe->writer_closed == 0 && pipe->reader_closed == 0){
			Cond_Broadcast(& pipe->cv_writers);
  			kernel_wait(& pipe->cv_readers,SCHED_PIPE);
  		}
  		pipe->ref_count_reader--;

  		// An exei kleisei h pleura tou prepei na stamathsei.
		if(pipe->reader_closed == 1) return count;

  		// an uparxei kati na diavasei to pernei kai sinexizei sto epomeno.
  		if(pipe->bufferElementsCount > 0){
	  		pipe->last_read_pos++;
	  		if (pipe->last_read_pos >= BUFFER_SIZE)
	  			pipe->last_read_pos = 0; // kuklos

	  		buf[count] = pipe->buffer[pipe->last_read_pos];
	  		
	  		pipe->bufferElementsCount--;
	  	}
  	}

  	// eleu8erw8hke xwros ara ksipname tous writer.
	Cond_Broadcast(& pipe->cv_writers);
	return count;
}


int pipe_reader_close(void* pipe_obj)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;

	// Exei kleisei idh auth meria.
	if(pipe->reader_closed)
		return 0;

	//kleisimo prwtos
	if (!pipe->writer_closed){
		if(!pipe->ref_count_reader)
			pipe->reader_closed = 1;
		Cond_Broadcast(& pipe->cv_writers);
		return 0;
	}

	//kleisimo deuteros 
	if(!pipe->ref_count_reader) 
		free(pipe);
	return 0;
}


int pipe_write(void* pipe_obj, const char *buf, unsigned int size)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;
	unsigned int count = 0;

	// Exei kleisei to read, den prepei na kanei write. Epishs an exei kleisei h pleura tou prepei na stamathsei.
	if(pipe->reader_closed || pipe->writer_closed) 
		return -1;

  	for(; count < size; count++) {

  		pipe->ref_count_writer++;

  		/*	An o buffer einai gematos kai yparxei kapios na diavasei kimizoume ton write.
  			Ksipnaei to read epeidh grafthke xwros h o buffer einai gematos.	*/
  		while(pipe->bufferElementsCount == BUFFER_SIZE && pipe->reader_closed == 0 && pipe->writer_closed == 0){

		  	// An grapsei olo ton pinaka kai 8elei ki allo prepei na ksipnsei tous readers prin koimi8ei.
			Cond_Broadcast(& pipe->cv_readers);
  			kernel_wait(& pipe->cv_writers,SCHED_PIPE);
  		}
  		pipe->ref_count_writer--;

		// Exei kleisei to read, den prepei na kanei write. 
		if(pipe->reader_closed) 
			return -1;

		// Epishs an exei kleisei h pleura tou prepei na stamathsei.
		if(pipe->writer_closed)
			return count;

		// Grafei kai sunexizei sto epomeno an uparxei xwros.
		if(pipe->bufferElementsCount < BUFFER_SIZE){
	  		pipe->last_write_pos++;
	  		if (pipe->last_write_pos >= BUFFER_SIZE)
	  			pipe->last_write_pos = 0; //kuklos

	  		pipe->buffer[pipe->last_write_pos] = buf[count];
	  		pipe->bufferElementsCount ++;
	  	}
  	}

  	// Mphkan stoixeia kai prepei na ksipnisoun oi writers.
  	if(pipe->ref_count_reader)
		Cond_Broadcast(& pipe->cv_readers);
	return count;
}


int pipe_writer_close(void* pipe_obj)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;

	// Exei kleisei idh auth meria.
	if(pipe->writer_closed)
		return 0;

	//klisimo prwtos
	if (!pipe->reader_closed){
		if(!pipe->ref_count_writer)//
			pipe->writer_closed = 1;
		Cond_Broadcast(& pipe->cv_readers);
		return 0;
	}

	//kleisimo deuteros
	if(!pipe->ref_count_writer) 
		free(pipe);
	return 0;
}


static file_ops pipe_reader_ops = {
  .Open = NULL,
  .Read = pipe_read,
  .Write = NULL,
  .Close = pipe_reader_close
};

static file_ops pipe_writer_ops = {
  .Open = NULL,
  .Read = NULL,
  .Write = pipe_write,
  .Close = pipe_writer_close
};


int sys_Pipe(pipe_t* pipe_id)
{
	Fid_t pipe_fids[2] = {0, 0};
	FCB* pipe_FCBs[2] = {NULL, NULL};

	// elengxos gia to reserve
	if(!FCB_reserve(2, (Fid_t*) &pipe_fids, (FCB**) &pipe_FCBs))
		return -1;

	create_pipe(pipe_FCBs);

	// pipe's fids
	pipe_id->read = pipe_fids[0];
	pipe_id->write = pipe_fids[1];

	return 0;
}


void create_pipe(FCB** pipe_FCBs)
{
	pipe_CB* pipe = (pipe_CB*) xmalloc(sizeof(pipe_CB));

	// Init pipe's variables. 
 	pipe->reader_closed = 0;
 	pipe->writer_closed = 0;

  	pipe->last_read_pos = -1;
  	pipe->last_write_pos = -1;

  	pipe->bufferElementsCount = 0;

  	pipe->cv_readers = COND_INIT; 
  	pipe->cv_writers = COND_INIT;
  	pipe->ref_count_writer = 0;
  	pipe->ref_count_reader = 0;

  	// Gemisma fcbs.
  	pipe_FCBs[0]->streamobj = pipe;
  	pipe_FCBs[0]->streamfunc = &pipe_reader_ops;

  	pipe_FCBs[1]->streamobj = pipe;
  	pipe_FCBs[1]->streamfunc = &pipe_writer_ops;
}