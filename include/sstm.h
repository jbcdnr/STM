#ifndef _SSTM_H_
#define	_SSTM_H_

#include <setjmp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "sstm_alloc.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define DEBUG 0

#define TTAS
#include "lock_if.h"
#include "atomic_ops_if.h"

  /* **************************************************************************************************** */
  /* structures */
  /* **************************************************************************************************** */


#define LIST_INITIAL_SIZE 32
#define LIST_EXPEND_FACTOR 4
#define HASH_MODULO 1024

  typedef struct record_t
  {
    volatile uintptr_t* address;
    uintptr_t value;
    size_t version;
  } record_t;

  struct nodee_t;
  typedef struct nodee_t
  {
    record_t record;
    struct nodee_t* next;
  } nodee_t;

  typedef struct array_list_t
  {
    record_t* array;
    size_t size;
    size_t capacity;
  } array_list_t;

  size_t hash_address(volatile uintptr_t* addr);

  void init_array_list(array_list_t* ls);

  void append_array_list(array_list_t* ls, volatile uintptr_t* address, uintptr_t value, size_t version);

  void free_array_list(array_list_t* ls);

  void free_linked_list(nodee_t* ls);

  typedef struct sstm_metadata
  {
    array_list_t read_set;
    size_t read_snapshot_timestamp;
    nodee_t* write_set[HASH_MODULO]; // TODO put to NULL

    sigjmp_buf env;		/* Environment for setjmp/longjmp */
    size_t id;
    size_t n_commits;
    size_t n_aborts;
  } sstm_metadata_t;

  typedef struct sstm_metadata_global
  {
    volatile size_t clock;
    volatile size_t locks[HASH_MODULO];

    size_t n_commits;
    size_t n_aborts;
  } sstm_metadata_global_t;


extern __thread sstm_metadata_t sstm_meta;
extern sstm_metadata_global_t sstm_meta_global;


  /* **************************************************************************************************** */
  /* TM start/stop macros macros */
  /* **************************************************************************************************** */

#define TM_START()				\
  sstm_start();

#define TM_STOP()				\
  sstm_stop();

#define TM_THREAD_START()			\
  sstm_thread_start();

#define TM_THREAD_STOP()			\
  sstm_thread_stop();

#define TM_STATS(dur_s)				\
  sstm_print_stats(dur_s);


  /* **************************************************************************************************** */
  /* TM macros */
  /* **************************************************************************************************** */

#define TX_START()					\
  { PRINTD("|| Starting new tx\n");			\
    short int reason;					\
    if ((reason = sigsetjmp(sstm_meta.env, 0)) != 0)	\
      {							\
	sstm_tx_cleanup();				\
	PRINTD("|| restarting due to %d\n", reason);	\
      }							\
  }

#define TX_COMMIT()				\
  sstm_tx_commit();				\
  PRINTD("|| commited tx (%zu)\n", sstm_meta.n_commits);     

#define TX_ABORT(reason)			\
  PRINTD("|| aborting tx (%d)\n", reason);	\
  siglongjmp(sstm_meta.env, reason);

#define TX_LOAD(addr)				\
  sstm_tx_load((volatile uintptr_t*) addr)

#define TX_STORE(addr, val)			\
  sstm_tx_store((volatile uintptr_t*) addr, (uintptr_t) val)

#define TX_MALLOC(size)				\
  sstm_tx_alloc(size)

#define TX_FREE(mem)				\
  sstm_tx_free(mem)


  /* **************************************************************************************************** */
  /* externs */
  /* **************************************************************************************************** */

  extern void sstm_start();
  /* terminates the TM runtime
     (e.g., deallocates the locks that the system uses ) 
  */
  extern void sstm_stop();
  /* prints the TM system stats
****** DO NOT TOUCH *********
*/
  extern void sstm_print_stats(double dur_s);
  /* initializes thread local data
     (e.g., allocate a thread local counter)
  */
  extern void sstm_thread_start();
  /* terminates thread local data
     (e.g., deallocate a thread local counter)
     ****** DO NOT CHANGE THE EXISTING CODE*********   
     */
  extern void sstm_thread_stop();
  /* transactionally reads the value of addr
   */
  extern inline uintptr_t sstm_tx_load(volatile uintptr_t* addr);
  /* transactionally writes val in addr
   */
  extern inline void sstm_tx_store(volatile uintptr_t* addr, uintptr_t val);
  /* cleaning up in case of an abort 
     (e.g., flush the read or write logs)
  */
  extern void sstm_tx_cleanup();
  /* tries to commit a transaction
     (e.g., validates some version number, and/or
     acquires a couple of locks)
  */
  extern void sstm_tx_commit();

  size_t validate();

  void clear_transaction();

  /* **************************************************************************************************** */
  /* help functions */
  /* **************************************************************************************************** */

  static inline void
  print_id(size_t id, const char* format, ... )
  {
    va_list args;
    fprintf( stdout, "[%03zu]: ", id);
    va_start( args, format );
    vfprintf( stdout, format, args );
    va_end( args );
  }


#if DEBUG == 1
#define PRINTD(args...) print_id(sstm_meta.id, args);
#else
#define PRINTD(args...) 
#endif


#ifdef	__cplusplus
}
#endif

#endif	/* _SSTM_H_ */

