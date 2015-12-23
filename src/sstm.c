#include "sstm.h"

LOCK_LOCAL_DATA;
__thread sstm_metadata_t sstm_meta;	 /* per-thread metadata */
sstm_metadata_global_t sstm_meta_global; /* global metadata */

/* initializes the TM runtime 
   (e.g., allocates the locks that the system uses ) 
*/
void
sstm_start()
{
  sstm_meta_global.global_lock = 0;
}

/* terminates the TM runtime
   (e.g., deallocates the locks that the system uses ) 
*/
void
sstm_stop()
{
}


/* initializes thread local data
   (e.g., allocate a thread local counter)
 */
void
sstm_thread_start()
{
  init_list(&sstm_meta.readers);
  init_list(&sstm_meta.writers);

  printf("readers cap: %i, writers cap: %i\n", sstm_meta.readers.capacity, sstm_meta.writers.capacity);
}

/* terminates thread local data
   (e.g., deallocate a thread local counter)
****** DO NOT CHANGE THE EXISTING CODE*********
****** Feel free to add more code *************
 */
void
sstm_thread_stop()
{
  free_list(&sstm_meta.readers);
  free_list(&sstm_meta.writers);

  __sync_fetch_and_add(&sstm_meta_global.n_commits, sstm_meta.n_commits);
  __sync_fetch_and_add(&sstm_meta_global.n_aborts, sstm_meta.n_aborts);
}


/* transactionally reads the value of addr
 * On a more complex than GL-STM algorithm,
 * you need to do more work than simply reading the value.
*/
inline uintptr_t
sstm_tx_load(volatile uintptr_t* addr)
{ 

  printf("readers @ %i, writers @ %i\n", &sstm_meta.readers, &sstm_meta.writers);

  printf("load - 0\n");

  // check if written in addr and return value if so
  int i;
  for (i = 0; i < sstm_meta.writers.size; i++) {
    cell_t* cell = &sstm_meta.writers.array[i];
    if(cell->address == addr) {
      return cell->value;
    }
  }

  printf("load - 1\n");

  uintptr_t val = *addr;
  while (sstm_meta.snapshot != sstm_meta_global.global_lock) {
    sstm_meta.snapshot = validate();
    val = *addr;
  }

  printf("load - 2\n");

  append_list(&sstm_meta.readers, addr, val);

  printf("load - 3\n");

  return val;
}

/* transactionally writes val in addr
 * On a more complex than GL-STM algorithm,
 * you need to do more work than simply reading the value.
*/
inline void
sstm_tx_store(volatile uintptr_t* addr, uintptr_t val)
{
  printf("store - 0\n");

  // update old value if any
  int i;
  for (i = 0; i < sstm_meta.writers.size; i++) {
    cell_t* cell = &sstm_meta.writers.array[i];
    if(cell->address == addr) {
      cell->value = val;
      return;
    }
  }


  printf("store - 1\n");

  append_list(&sstm_meta.writers, addr, val);

  printf("store - 2\n");
}

/* cleaning up in case of an abort 
   (e.g., flush the read or write logs)
*/
void
sstm_tx_cleanup()
{
  clear_transaction();
  sstm_alloc_on_abort();
  sstm_meta.n_aborts++;
}

/* tries to commit a transaction
   (e.g., validates some version number, and/or
   acquires a couple of locks)
 */
void
sstm_tx_commit()
{

  printf("commit - 0\n");

  if (sstm_meta.writers.size > 0) {


    printf("commit - 1\n");

    while (CAS_U64(
      &sstm_meta_global.global_lock, 
      sstm_meta.snapshot, 
      sstm_meta.snapshot + 1) != sstm_meta.snapshot) 
    {
      sstm_meta.snapshot = validate();
    }


    printf("commit - 2\n");

    int i;
    for (i = 0; i < sstm_meta.writers.size; i++) {
      cell_t* cell = &sstm_meta.writers.array[i];
      *(cell->address) = cell->value;
    }   

    printf("commit - 3\n");

    sstm_alloc_on_commit(); // TODO
    sstm_meta_global.global_lock = sstm_meta.snapshot + 2;

  }

  clear_transaction();
  sstm_meta.n_commits++;		
}


size_t validate() {


  printf("validate - 0\n");

  while (1) {
    size_t time = sstm_meta_global.global_lock;
    if((time & 1) != 0) {
      continue;
    }

    printf("validate - 1\n");


    int i;
    for (i = 0; i < sstm_meta.readers.size; i++) {
      cell_t* cell = &sstm_meta.readers.array[i];
      if(*cell->address != cell->value) {
        TX_ABORT(1000);
      }
    }

    printf("validate - 2\n");

    if (time == sstm_meta_global.global_lock) {
      return time;
    }
  }
}

void init_list(list_t* ls) {
  ls->size = 0;
  ls->capacity = LIST_INITIAL_SIZE;
  ls->array = calloc(LIST_INITIAL_SIZE, sizeof(cell_t));
  printf("Init list @ %i\n", ls);
}

void append_list(list_t* ls, volatile uintptr_t* address, uintptr_t value) {
  printf("append - 0 - size %i - cap %i\n", ls->size, ls->capacity);

  while (ls->size >= ls->capacity) {
    printf("append expend %i -> %i\n", ls->capacity, ls->capacity * 2);
    ls->array = realloc(ls->array, ls->capacity * 2 * sizeof(cell_t));
    ls->capacity *= 2;
  }
  ls->array[ls->size].address = address;
  ls->array[ls->size].value = value;
  ls->size++;
}

void free_list(list_t* ls) {
  free(ls->array);
  ls->array = NULL;
}

void clear_transaction() {
  sstm_meta.readers.size = 0;
  sstm_meta.writers.size = 0;
}


/* prints the TM system stats
****** DO NOT TOUCH *********
*/
void
sstm_print_stats(double dur_s)
{
  printf("# Commits: %-10zu - %.0f /s\n",
	 sstm_meta_global.n_commits,
	 sstm_meta_global.n_commits / dur_s);
  printf("# Aborts : %-10zu - %.0f /s\n",
	 sstm_meta_global.n_aborts,
	 sstm_meta_global.n_aborts / dur_s);
}
