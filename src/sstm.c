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
}

/* terminates thread local data
   (e.g., deallocate a thread local counter)
****** DO NOT CHANGE THE EXISTING CODE*********
****** Feel free to add more code *************
 */
void
sstm_thread_stop()
{
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
  // check if written in addr and return value if so
  list_t* curr = sstm_meta.writers;
  while (curr != NULL) {
    if (curr->address == addr) {
      return curr->value;
    }
    curr = curr->next;
  }

  uintptr_t val = *addr;
  while (sstm_meta.snapshot != sstm_meta_global.global_lock) {
    sstm_meta.snapshot = validate();
    val = *addr;
  }

  // prepend (addr, val) to reading
  list_t* newHead = (list_t*) malloc(sizeof(list_t)); // TODO check success ?
  newHead->address = addr;
  newHead->value = val;
  newHead->next = sstm_meta.readers;
  sstm_meta.readers = newHead;

  return val;
}

/* transactionally writes val in addr
 * On a more complex than GL-STM algorithm,
 * you need to do more work than simply reading the value.
*/
inline void
sstm_tx_store(volatile uintptr_t* addr, uintptr_t val)
{
  printf("store %i in %i\n", addr, val);

  // find addr if existing
  list_t* curr = sstm_meta.writers;
  print(sstm_meta.writers);
  while (curr != NULL && curr->address != addr) {
    curr = curr->next;
  }

  // update the value or create the update node
  if (curr == NULL) {
    list_t* newHead = (list_t*) malloc(sizeof(list_t)); // TODO check success ?
    newHead->address = addr;
    newHead->value = val;
    newHead->next = sstm_meta.writers;
    sstm_meta.writers = newHead;
  } else {
    curr->value = val;
  }

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
  printf("commit for #%i\n", sstm_meta.id);
  printf("readers: ");
  print(sstm_meta.readers);
  printf("writers: ");
  print(sstm_meta.writers);

  if (sstm_meta.writers == NULL) {
    clear_transaction();
    return;
  }


  // TODO maybe wrong return check for CAS (if not bool)
  while (CAS_U64(
    &sstm_meta_global.global_lock, 
    sstm_meta.snapshot, 
    sstm_meta.snapshot + 1) != sstm_meta.snapshot) 
  {
    sstm_meta.snapshot = validate();
  }

  list_t* curr = sstm_meta.writers;
  while (curr != NULL) {
    *curr->address = curr->value;
    curr = curr->next;
  }
       
  sstm_alloc_on_commit(); // TODO
  sstm_meta_global.global_lock = sstm_meta.snapshot + 2;

  clear_transaction();
  sstm_meta.n_commits++;		
}


size_t validate() {
  while (1) {
    size_t time = sstm_meta_global.global_lock;
    if((time & 1) != 0) {
      continue;
    }

    printf("validate for #%i\n", sstm_meta.id);
    printf("readers: ");
    print(sstm_meta.readers);
    list_t* curr = sstm_meta.readers;
    while (curr != NULL) {
      if (*curr->address != curr->value) {
        printf("Abort transaction during validation\n");
        TX_ABORT(1000);
        return; // TODO check
      }
      curr = curr->next;
    }

    if (time == sstm_meta_global.global_lock) {
      return time;
    }
  }
}

size_t length(list_t* ls) {
  if(ls == NULL)
    return 0;
  else
    return 1 + length(ls->next);
}

void print(list_t* ls) {
  if(ls == NULL)
    printf("NULL\n");
  else {
    printf("(%i, %i) -> ", ls->address, ls->value);
    print(ls->next);
  }
}


void clear_transaction() {
  list_t* next;
  list_t* curr = sstm_meta.readers;
  while (curr != NULL) {
    next = curr->next;
    free(curr);
    curr = next;
  }

  curr = sstm_meta.writers;
  while (curr != NULL) {
    next = curr->next;
    free(curr);
    curr = next;
  }

  sstm_meta.readers = NULL;
  sstm_meta.writers = NULL;
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
