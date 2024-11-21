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

  		/*	If the buffer is empty and there is no one to write to, it must return with as much data as it managed to read. 
		When it has reached eof it will return 0 */
  		if(pipe->bufferElementsCount == 0 && pipe->writer_closed == 1)
  			return count;

  		pipe->ref_count_reader++;

  		/*	If the buffer is empty and there is someone to write to, we sleep the read.  
		It wakes up the write because no space is freed or the buffer is empty.	*/
  		while(pipe->bufferElementsCount == 0 && pipe->writer_closed == 0 && pipe->reader_closed == 0){
			Cond_Broadcast(& pipe->cv_writers);
  			kernel_wait(& pipe->cv_readers,SCHED_PIPE);
  		}
  		pipe->ref_count_reader--;

  		// If his side is closed he must stop.
		if(pipe->reader_closed == 1) return count;

  		// If there is something to read, he takes it and continues to the next one.
  		if(pipe->bufferElementsCount > 0){
	  		pipe->last_read_pos++;
	  		if (pipe->last_read_pos >= BUFFER_SIZE)
	  			pipe->last_read_pos = 0; // Circle

	  		buf[count] = pipe->buffer[pipe->last_read_pos];
	  		
	  		pipe->bufferElementsCount--;
	  	}
  	}

  	// Space has been freed so we are waking up the writer.
	Cond_Broadcast(& pipe->cv_writers);
	return count;
}


int pipe_reader_close(void* pipe_obj)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;

	// This side is closed.
	if(pipe->reader_closed)
		return 0;

	//Close first
	if (!pipe->writer_closed){
		if(!pipe->ref_count_reader)
			pipe->reader_closed = 1;
		Cond_Broadcast(& pipe->cv_writers);
		return 0;
	}

	//Close second
	if(!pipe->ref_count_reader) 
		free(pipe);
	return 0;
}


int pipe_write(void* pipe_obj, const char *buf, unsigned int size)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;
	unsigned int count = 0;

	// The read is closed, no write should be done. Also if his side is closed, he must stop..
	if(pipe->reader_closed || pipe->writer_closed) 
		return -1;

  	for(; count < size; count++) {

  		pipe->ref_count_writer++;

  		/*	If the buffer is full and there is something to read, we sleep the write. 
		Wake up the read because space is written or the buffer is full	*/
  		while(pipe->bufferElementsCount == BUFFER_SIZE && pipe->reader_closed == 0 && pipe->writer_closed == 0){

		  	// If he writes the table and wants more he has to wake up the readers before he falls asleep.
			Cond_Broadcast(& pipe->cv_readers);
  			kernel_wait(& pipe->cv_writers,SCHED_PIPE);
  		}
  		pipe->ref_count_writer--;

		// Read closed, there should be no write. 
		if(pipe->reader_closed) 
			return -1;

		// Read closed, side should be closed.
		if(pipe->writer_closed)
			return count;

		// Writes and continues to the next one if there is space.
		if(pipe->bufferElementsCount < BUFFER_SIZE){
	  		pipe->last_write_pos++;
	  		if (pipe->last_write_pos >= BUFFER_SIZE)
	  			pipe->last_write_pos = 0; //Circle

	  		pipe->buffer[pipe->last_write_pos] = buf[count];
	  		pipe->bufferElementsCount ++;
	  	}
  	}

  	// New elements, so the writers need to wake up.
  	if(pipe->ref_count_reader)
		Cond_Broadcast(& pipe->cv_readers);
	return count;
}


int pipe_writer_close(void* pipe_obj)
{
	pipe_CB* pipe = (pipe_CB*) pipe_obj;

	// Side closed
	if(pipe->writer_closed)
		return 0;

	//Close first
	if (!pipe->reader_closed){
		if(!pipe->ref_count_writer)//
			pipe->writer_closed = 1;
		Cond_Broadcast(& pipe->cv_readers);
		return 0;
	}

	//Close second
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

	// Control for reserve
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

  	// Fill fcbs
  	pipe_FCBs[0]->streamobj = pipe;
  	pipe_FCBs[0]->streamfunc = &pipe_reader_ops;

  	pipe_FCBs[1]->streamobj = pipe;
  	pipe_FCBs[1]->streamfunc = &pipe_writer_ops;
}