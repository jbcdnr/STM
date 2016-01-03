#include "sstm.h"

LOCK_LOCAL_DATA;
__thread sstm_metadata_t sstm_meta;	 /* per-thread metadata */
sstm_metadata_global_t sstm_meta_global; /* global metadata */

/* initializes the TM runtime 
   (e.g., allocates the locks that the system uses ) 
*/
void sstm_start() {

  PRINTD("START GLOBAL 0\n");

  sstm_meta_global.clock = 0;
  int i;
  for(i = 0; i < HASH_MODULO; i++) { // TODO useful ?
    sstm_meta_global.locks[i] = 0;
  }

  PRINTD("START GLOBAL 1\n");
}

/* terminates the TM runtime
   (e.g., deallocates the locks that the system uses ) 
*/
void sstm_stop() {
}


/* initializes thread local data
   (e.g., allocate a thread local counter)
 */
void sstm_thread_start() {

  PRINTD("START THREAD 0\n");

  init_array_list(&sstm_meta.read_set);
  // TODO check if need to put writer set to NULL
}

/* terminates thread local data
   (e.g., deallocate a thread local counter)
****** DO NOT CHANGE THE EXISTING CODE*********
****** Feel free to add more code *************
 */
void
sstm_thread_stop()
{
  free_array_list(&sstm_meta.read_set);

  __sync_fetch_and_add(&sstm_meta_global.n_commits, sstm_meta.n_commits);
  __sync_fetch_and_add(&sstm_meta_global.n_aborts, sstm_meta.n_aborts);
}


/* transactionally reads the value of addr
 * On a more complex than GL-STM algorithm,
 * you need to do more work than simply reading the value.
*/
inline uintptr_t sstm_tx_load(volatile uintptr_t* addr) {

  size_t hash = hash_address(addr);
  size_t before = sstm_meta_global.locks[hash];
  size_t value;

  PRINTD("LOAD addr %i - lock %i\n", addr, before);

  // lock is owned by someone
  if (before & 1) {
    // it is mine
    if (before >> 1 == sstm_meta.id) {
      // check if we have written in it
      nodee_t* curr = sstm_meta.write_set[hash];
      while (curr != NULL && curr->record.address != addr) {
        curr = curr->next;
      }

      // if not written in, read it otherwise take last written value
      if(curr == NULL) {
        value = *addr;
      } else {
        value = curr->record.value;
      }
    } else { // hold by someone else
      TX_ABORT(10);
    }
  } else {
    PRINTD("LOAD nobody owns\n");
    value = *addr;
    size_t after = sstm_meta_global.locks[hash];

    if (after != before) { // inconsistent read
      PRINTD("LOAD abort inconsistent\n");
      TX_ABORT(10);
    }

    // // check if we must extend the snapshot
    // if (sstm_meta.read_snapshot_timestamp < after) {
    //   PRINTD("LOAD must create snapshot\n");
    //   int i;
    //   for (i = 0; i < sstm_meta.read_set.size; i++) {
    //     record_t* record = &sstm_meta.read_set.array[i];
    //     if (record->version != sstm_meta_global.locks[hash_address(record->address)]) {
    //       PRINTD("LOAD abort snapshot\n");
    //       TX_ABORT(10);
    //     }
    //   }
    //   // update time stamp of snapshot
    //   sstm_meta.read_snapshot_timestamp = after;
    // }

    append_array_list(&sstm_meta.read_set, addr, after, 0); // we don't care about the read value
  }


  return value;
}

/* transactionally writes val in addr
 * On a more complex than GL-STM algorithm,
 * you need to do more work than simply reading the value.
*/
inline void sstm_tx_store(volatile uintptr_t* addr, uintptr_t val) {

  size_t hash = hash_address(addr);
  size_t lock = sstm_meta_global.locks[hash];

  PRINTD("STORE addr %i - val %i - lock %i\n", addr, val, lock);

  size_t alreadyIn = 0;

  // owned by someone
  if (lock & 1) {
    // myself
    if (lock >> 1 == sstm_meta.id) {
      alreadyIn = 1;
    } else { // someone else
      TX_ABORT(1);
    }
  } 

  if(alreadyIn) {
    PRINTD("STORE already in lock\n");
    // check if we have written in it
    nodee_t* curr = sstm_meta.write_set[hash];
    while (curr != NULL && curr->record.address != addr) {
      curr = curr->next;
    }
    if (curr != NULL) {
      PRINTD("STORE already written in value\n");
      curr->record.value = val;
      return;
    } 
  } else { // need to acquire the lock
    size_t prev;
    while (1) {
      PRINTD("STORE try new lock value %i\n", (sstm_meta.id << 1) | 1);
      // TODO problem here

      // prev = CAS_U64(&sstm_meta.write_set[hash], 1000, 2000);
      // PRINTD("STORE lock %i - return %i\n", sstm_meta.write_set[hash], prev);

      prev = CAS_U64(&sstm_meta_global.locks[hash], lock, (sstm_meta.id << 1) | 1);
      PRINTD("STORE lock %i - return %i\n", sstm_meta_global.locks[hash], prev);
      if (lock == prev) { // success
        PRINTD("STORE lock acquired\n");
        break;
      }
      if (prev & 1) {
        PRINTD("STORE abort\n");
        TX_ABORT(2);
      }
    }
  }

  // add the new edit to the set
  nodee_t* newHead = malloc(sizeof(nodee_t));
  newHead->record.value = val;
  newHead->record.address = addr;
  newHead->record.version = 0; // we don't care about version
  newHead->next = sstm_meta.write_set[hash];
  sstm_meta.write_set[hash] = newHead;

  PRINTD("AFTER STORE lock %i\n", sstm_meta_global.locks[hash]);
}

/* cleaning up in case of an abort 
   (e.g., flush the read or write logs)
*/
void sstm_tx_cleanup() {
  sstm_alloc_on_abort();
  clear_transaction();
  sstm_meta.n_aborts++;
}

/* tries to commit a transaction
   (e.g., validates some version number, and/or
   acquires a couple of locks)
 */
void sstm_tx_commit() {

  PRINTD("COMMIT 0\n");

  size_t timestamp = IAF_U64(&sstm_meta_global.clock);

  sstm_alloc_on_commit(); // free the memory


  PRINTD("COMMIT 1\n");

  // write all the values
  int i;
  for (i=0; i < HASH_MODULO; i++) {
    nodee_t* curr = sstm_meta.write_set[i];

    while (curr != NULL) {
      PRINTD("COMMIT 4 -- %i\n", curr);
      PRINTD("COMMIT 4 -- %i\n", curr->record.address);
      *curr->record.address = curr->record.value;
      curr = curr->next;
      PRINTD("COMMIT 5\n");
    }

    // change the version
    if (sstm_meta.write_set[i] != NULL) {
      CAS_U64(&sstm_meta_global.locks[i], sstm_meta_global.locks[i], timestamp << 1);
    }
  }

  PRINTD("COMMIT 7\n");

  clear_transaction();
  sstm_meta.n_commits++;		
}

void clear_transaction() {
  // reset the readers and writers lists
  int i;
  for (i=0; i < HASH_MODULO; i++) {
    nodee_t* curr = sstm_meta.write_set[i];
    free_linked_list(curr);
    sstm_meta.write_set[i] = NULL;
  }
  sstm_meta.read_set.size = 0;
}

size_t hash_address(volatile uintptr_t* addr) {
  return ((size_t) addr / 4) % HASH_MODULO;
}

/*
 * LIST UTILITY FUNCTIONS
 */ 

void init_array_list(array_list_t* ls) {
  ls->size = 0;
  ls->capacity = LIST_INITIAL_SIZE;
  ls->array = calloc(LIST_INITIAL_SIZE, sizeof(record_t));
}

void append_array_list(array_list_t* ls, volatile uintptr_t* address, uintptr_t value, size_t version) {

  // extend the capacity of the array_list if needed
  while (ls->size >= ls->capacity) {
    ls->array = realloc(ls->array, ls->capacity * LIST_EXPEND_FACTOR * sizeof(record_t));
    ls->capacity *= LIST_EXPEND_FACTOR;
  }

  ls->array[ls->size].address = address;
  ls->array[ls->size].value = value;
  ls->array[ls->size].version = version;
  ls->size++;
}

void free_array_list(array_list_t* ls) {
  free(ls->array);
  ls->array = NULL;
}

void free_linked_list(nodee_t* ls) {
  nodee_t* next;
  while (ls != NULL) {
    next = ls->next;
    free(ls);
    ls = next;
  }
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
