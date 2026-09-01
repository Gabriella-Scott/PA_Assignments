/**
 * @mainpage Process Simulation
 *
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "proc_structs.h"
#include "proc_syntax.h"
#include "logger.h"
#include "manager.h"

#include <stdbool.h>


#define LOWEST_PRIORITY INT_MAX // 0 is highest, setting INT_kAX as lowest

static pcb_queue_t terminatedq;
static pcb_queue_t waitingq;
static pcb_queue_t readyq;
static resource_t *system_resources;
static int global_process_count; // for terminate
static int all_processes_loaded;
static omp_nest_lock_t q_locks[3];
static omp_nest_lock_t a_lock;
static omp_nest_lock_t i_lock;
static omp_nest_lock_t r_lock;

bool_t terminate();
void schedule_fcfs(int num_thr);
void schedule_rr(int quantum, int num_thr);
void schedule_priority(int num_thr);
bool_t higher_priority(pcb_t *, pcb_t *);

void execute_instr(pcb_t *proc);
void request_resource(pcb_t *proc);
void release_resource(pcb_t *proc);
int queue_count(pcb_queue_t *queue);

pcb_queue_t get_block_processes();
resource_t *get_waiting_resource(char *resource_name);
void detect_deadlock(void);

void enqueue_pcb(pcb_t *proc, pcb_queue_t *queue, int status);
pcb_t *dequeue_pcb(pcb_queue_t *queue);
pcb_t *remove_pcb(pcb_queue_t *queue, pcb_t *pcb);
pcb_t *remove_pcb_unlock(pcb_queue_t *queue, pcb_t *pcb);
pcb_t *get_prev(pcb_queue_t *queue, pcb_t *pcb, bool_t *found);

int get_num_threads(int num_args, char **argv);
char *get_data(int num_args, char **argv);
int get_algo(int num_args, char **argv);
int get_time_quantum(int num_args, char **argv);
void print_args(int num_thr, char *data, int sched, int tq);

int main(int argc, char **argv)
{
  int num_thr = get_num_threads(argc, argv);
  char *data = get_data(argc, argv);
  int scheduler = get_algo(argc, argv);
  int time_quantum = get_time_quantum(argc, argv);
  print_args(num_thr, data, scheduler, time_quantum);
  bool_t success = FALSE;

  if (strcmp(data, "generate") == 0)
  {
#ifdef DEBUG_MNGR
    printf("****Generate processes and initialise the system\n");
#endif
    success = init_loader_from_generator();
  }
  else
  {
#ifdef DEBUG_MNGR
    printf("Parse process file and initialise the system: %s \n", data);
#endif
    success = init_loader_from_files(data);
  }

  if (success)
  {
    init_system();
    system_resources = get_resources();
    printf("***********Scheduling processes************\n");
    schedule_processes(num_thr, scheduler, time_quantum);
    dealloc_data_structures();
  }
  else
  {
    printf("Error: no processes to schedule\n");
  }

  return EXIT_SUCCESS;
}

/**
 * @brief The linked list of loaded processes is moved to the readyqueue.
 *    The waiting and terminated queues are intialised to empty
 */
void init_system(void)
{
#ifdef DEBUG_MNGR
  fprintf(stderr, "Initialising system queues and locks\n");
#endif

  readyq.first = longterm_scheduler();
  // No process loaded into ready queue
  pcb_t *current = readyq.first;
  readyq.last = NULL;
  int process_count = 0; // to track processes
  // Sets the state of each PCB and find the last one in queue
  while (current != NULL)
  {
    current->state = READY;
    readyq.last = current; // Update last process in the queue
    current = current->next;
    process_count++;
  }

#ifdef DEBUG
  fprintf(stderr, "Loaded %d processes intro ready queue\n", process_count);
#endif

  // Initialise waiting and terminated queues
  readyq.lock_index = 0;
  waitingq.last = NULL;
  waitingq.first = NULL;
  waitingq.lock_index = 1;
  terminatedq.last = NULL;
  terminatedq.first = NULL;
  terminatedq.lock_index = 2;

  omp_init_nest_lock(&q_locks[0]);
  omp_init_nest_lock(&q_locks[1]);
  omp_init_nest_lock(&q_locks[2]);
  omp_init_nest_lock(&a_lock);
  omp_init_nest_lock(&i_lock);
  omp_init_nest_lock(&r_lock);

  global_process_count = process_count;
  all_processes_loaded = 0;

  log_queue(readyq.first, "Ready");
  log_queue(waitingq.first, "Waiting");
  log_queue(terminatedq.first, "Terminated");
  log_msg("\n");
}

/** @brief Schedules each instruction of each process */
void schedule_processes(int num_thr, schedule_t sched_type, int quantum)
{
#pragma omp parallel num_threads(num_thr)
  {
    switch (sched_type)
    {
    case PRIOR:
      schedule_priority(num_thr);
      break;
    case RR:
      schedule_rr(quantum, num_thr);
      break;
    case FCFS:
      schedule_fcfs(num_thr);
      break;
    default:
      break;
    }
  }
}

/** @brief Return true when there are no more processes to schedule */
bool_t terminate()
{
  // Check if all processes have been loaded
  if (all_processes_loaded == 0)
  {
#ifdef DEBUG
    fprintf(stderr, "Not all processes loaded yet, continue scheduling\n");
#endif
    return FALSE;
  }
  else if (readyq.first != NULL) // Check if ready queue is empty
  {
#ifdef DEBUG
    fprintf(stderr, "Not all processes loaded yet, continue scheduling\n");
#endif
    return FALSE;
  }
  // Count processes in waiting and terminated states
  int waiting = queue_count(&waitingq);
  int terminated = queue_count(&terminatedq);
  int total = waiting + terminated;

  // If all processes are either waiting or terminated, terminate
  if (total == global_process_count)
  {
    return TRUE;
  }

  // Still processes that need to be scheduled
  return FALSE;
}

/**
 * @brief Count the number of processes in a queue
 */
int queue_count(pcb_queue_t *queue)
{
  omp_set_nest_lock(&q_locks[queue->lock_index]);
  pcb_t *q = queue->first; // Start at the first process in the queue
  int count = 0;
  // Traverse the linked list and increment the count for each process
  while (q != NULL)
  {
    count++;
    q = q->next; // Move to next process
  }
  omp_unset_nest_lock(&q_locks[queue->lock_index]);
  // Returning total number processes counted
  return count;
}

/**
 * @brief Call the longterm schedule to check for new arrivals
 * If there are new arrivals, call
 *  log_pcbs("New arrivals in ready queue", new_arrivals);
 */
void load_new_processes(void)
{
  omp_set_nest_lock(&a_lock);

#ifdef DEBUG
  fprintf(stderr, "Thread %d checking for new process arrivals\n", omp_get_thread_num());
#endif
  // Call longterm scheduler to get any new arrived processes
  pcb_t *new_arrivals = longterm_scheduler();
  if (new_arrivals != NULL)
  {
    pcb_t *curr = new_arrivals; // Go through list of net arrivals
    pcb_t *p;                   // Holds next pointer
    int process_count = 0;
    while (curr != NULL)
    {
      curr->state = READY; // Setting process state to ready
      p = curr->next;
      enqueue_pcb(curr, &readyq, READY); // Enqueue process to ready queue
      curr = p;                          // Move next process to list
      process_count++;
    }
    // Update with new arrivals
    global_process_count = global_process_count + process_count;

    // Log new arrivals in ready queue
    log_pcbs("New arrivals in ready queue", new_arrivals);
  }
  else
  {
    // If there are no new arrivals, mark that all processes have been loaded
    all_processes_loaded = 1;
#ifdef DEBUG
    fprintf(stderr, "Thread %d: All processes have been loaded\n", omp_get_thread_num());
#endif
  }
  omp_unset_nest_lock(&a_lock);
}

/**
 * @brief FCFS scheduler implementation
 *
 * Implements a non-preemptive FCFS scheduling algorithm.
 * Each thread dequeues and executes processes from the ready queue until all
 * processes are terminated / in deadlock.
 */
void schedule_fcfs(int num_thr)
{
  do
  {
    pcb_t *running_p = NULL;
#pragma omp critical
    {
      // Critical section to safely dequeue a process from the ready queue
      omp_set_nest_lock(&q_locks[0]);
      running_p = dequeue_pcb(&readyq); // Get first process from ready queue
      omp_unset_nest_lock(&q_locks[0]);
    }

    // If a process was successfully dequeued
    if (running_p != NULL)
    {
      running_p->state = RUNNING;
      // Executes all instructions until it terminates/goes to waiting
      while (running_p->state == RUNNING)
      {
        // Check if process has any instructions to execute
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          break;
        }
        omp_set_nest_lock(&i_lock);
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          omp_unset_nest_lock(&i_lock);
          break;
        }

        execute_instr(running_p); // Executing current instruction
        omp_unset_nest_lock(&i_lock);

        // Check for new process arrivals while executing current process
        load_new_processes();

        // If process is waiting, break out of loop
        if (running_p->state == WAITING)
        {
          break;
        }

        // Move to next instruction
        running_p->next_instruction = running_p->next_instruction->next;

        // If there are no more instructions, mark as terminated
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          break;
        }
      }
      // if process is not in waiting queue it must have terminated or still needs to run some instructions
      if (running_p->state == TERMINATED)
      {
#pragma omp critical
        {
          omp_set_nest_lock(&q_locks[2]);
          enqueue_pcb(running_p, &terminatedq, TERMINATED);
          omp_unset_nest_lock(&q_locks[2]);

#ifdef DEBUG
          fprintf(stderr, "Thread %d: Process is moved to terminated queue\n", omp_get_thread_num());
#endif
        }
      }
    }

  } while (terminate() == FALSE); // Continue until all processes are done

#pragma omp barrier
  {
  }
#pragma omp single
  {
#ifdef DEBUG
    fprintf(stderr, "Thread %d performing deadlock detection\n", omp_get_thread_num());
#endif

    detect_deadlock();
    omp_destroy_nest_lock(&q_locks[0]);
    omp_destroy_nest_lock(&q_locks[1]);
    omp_destroy_nest_lock(&q_locks[2]);
    omp_destroy_nest_lock(&a_lock);
    omp_destroy_nest_lock(&i_lock);
    omp_destroy_nest_lock(&r_lock);
#ifdef DEBUG
    fprintf(stderr, "Thread %d: All locks destroyed, scheduler terminating\n", omp_get_thread_num());
#endif
  }
}

/**
 * @brief Schedules processes usint the Round-Robin algorithm
 *
 * Implements a preemptive RR scheduling algorithm that executes each process
 * for fixed time quantum before context switching.
 */
void schedule_rr(int quantum, int num_thr)
{
#ifdef DEBUG
  fprintf(stderr, "Thread %d starting RR scheduler with quantum: %d\n", omp_get_thread_num(), quantum);
#endif
  do
  {
    pcb_t *running_p = NULL;
#pragma omp critical
    {
      omp_set_nest_lock(&q_locks[0]);
      running_p = dequeue_pcb(&readyq);
      omp_unset_nest_lock(&q_locks[0]);
    }
    if (running_p != NULL)
    {
      int r = 0; // Counter for the number of instructions executed in this quantum
      running_p->state = RUNNING;
      do
      {
        // The case were the process has no instructions to execute
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          break;
        }
        // Lock to protect instruction execution
        omp_set_nest_lock(&i_lock);
        execute_instr(running_p); // Execute current instruction
        omp_unset_nest_lock(&i_lock);

        // Check for new process arrivals
        load_new_processes();

        // If process waiting, break out of execution loop
        if (running_p->state == WAITING)
        {
          break;
        }
        // Move instruction pointer forward
        running_p->next_instruction = running_p->next_instruction->next;
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          break;
        }
        r++; // Increment instruction counter for this quantum
      } while (r < quantum); // Continue until quantum expires
      // If process is not in waiting queue it must have terminated or still needs to run some instructions
      if (running_p->state == TERMINATED)
      {
        // Move process to terminated queue
#pragma omp critical
        {
          omp_set_nest_lock(&q_locks[2]);
          enqueue_pcb(running_p, &terminatedq, TERMINATED);
          omp_unset_nest_lock(&q_locks[2]);
        }
      }
      else if (running_p->state == RUNNING)
      {
#pragma omp critical
        {
          omp_set_nest_lock(&q_locks[0]);
          enqueue_pcb(running_p, &readyq, READY);
          omp_unset_nest_lock(&q_locks[0]);
        }
      }
    }
  } while (terminate() == FALSE); // Continue until all processes are done

#pragma omp barrier
  {
  }
#pragma omp single
  {
#ifdef DEBUG
    fprintf(stderr, "Thread %d performing deadlock detection\n", omp_get_thread_num());
#endif

    detect_deadlock();
    omp_destroy_nest_lock(&q_locks[0]);
    omp_destroy_nest_lock(&q_locks[1]);
    omp_destroy_nest_lock(&q_locks[2]);
    omp_destroy_nest_lock(&a_lock);
    omp_destroy_nest_lock(&i_lock);
    omp_destroy_nest_lock(&r_lock);

#ifdef DEBUG
    fprintf(stderr, "Thread %d: All locks destroyed, scheduler terminating\n", omp_get_thread_num());
#endif
  }
}

/**
 * @brief Schedules processes using priority scheduling with preemption
 *
 * Implements a preemptive priority scheduling algorithm that always selects the highest priority
 * process from the ready queue.
 * If a higher priority process becomes ready while other process is running, the running
 * process is preempted and returned to the ready queue.
 */
void schedule_priority(int num_thr)
{
  do
  {
    // Select the highest priority process
    // Find highest priority process n the ready queue
    pcb_t *running_p = NULL;
    pcb_t *higher_priority_pcb = NULL;
    omp_set_nest_lock(&q_locks[0]);
    pcb_t *curr = readyq.first;

    // Find the highest priority process the ready queue
    while (curr != NULL)
    {
      if (higher_priority_pcb == NULL || higher_priority(curr, higher_priority_pcb))
      {
        higher_priority_pcb = curr;
      }
      curr = curr->next;
    }
    // If found a process to run, remove it from ready queue
    if (higher_priority_pcb != NULL)
    {
      running_p = remove_pcb(&readyq, higher_priority_pcb);
      omp_unset_nest_lock(&q_locks[0]);
      running_p->state = RUNNING;
    }
    else
    {
      omp_unset_nest_lock(&q_locks[0]);
    }

    // If we have a process to run
    if (running_p != NULL)
    {
      do
      {
        // Check if the process has any instructions left
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          break;
        }
        // Execute current instruction
        omp_set_nest_lock(&i_lock);
        execute_instr(running_p);
        omp_unset_nest_lock(&i_lock);

        // Check for new arrivals
        load_new_processes();

        // Handle process state changes
        if (running_p->state == WAITING)
        {
          break;
        }

        // Move to next instruction
        running_p->next_instruction = running_p->next_instruction->next;
        if (running_p->next_instruction == NULL)
        {
          running_p->state = TERMINATED;
          break;
        }
        // Check if you should preempt, by looking for higher priority process
        higher_priority_pcb = running_p;
        omp_set_nest_lock(&q_locks[0]);

        curr = readyq.first;
        while (curr != NULL)
        {
          if (higher_priority_pcb == NULL || higher_priority(curr, higher_priority_pcb))
          {
            higher_priority_pcb = curr;
          }
          curr = curr->next;
        }
        // Preempt the running process if we found a higher priority process
        if (higher_priority_pcb != running_p)
        {
          enqueue_pcb(running_p, &readyq, READY);               // Return current process to ready queue
          running_p = remove_pcb(&readyq, higher_priority_pcb); // Get the higher priority process
          omp_unset_nest_lock(&q_locks[0]);
          running_p->state = RUNNING;
        }
        else
        {
          omp_unset_nest_lock(&q_locks[0]);
        }

      } while (running_p != NULL); // Continue until process is preempted or finishes

      // Move terminated processes to the terminated queue
      if (running_p->state == TERMINATED)
      {
        enqueue_pcb(running_p, &terminatedq, TERMINATED);
      }
    }

  } while (terminate() == FALSE); // Continue untill all processes are done

  // Synchronise all threads at this barrier
#pragma omp barrier
  {
  }
#pragma omp single
  {
#ifdef DEBUG
    fprintf(stderr, "Thread %d performing deadlock detection\n", omp_get_thread_num());
#endif

    detect_deadlock();
    omp_destroy_nest_lock(&q_locks[0]);
    omp_destroy_nest_lock(&q_locks[1]);
    omp_destroy_nest_lock(&q_locks[2]);
    omp_destroy_nest_lock(&a_lock);
    omp_destroy_nest_lock(&i_lock);
    omp_destroy_nest_lock(&r_lock);

#ifdef DEBUG
    fprintf(stderr, "Thread %d: All locks destroyed, scheduler terminating\n", omp_get_thread_num());
#endif
  }
}

/** @brief Return TRUE if pr1 has a higher priority than pr2 */
bool_t higher_priority(pcb_t *pr1, pcb_t *pr2)
{
  if (pr1->priority < pr2->priority)
  {
    return TRUE;
  }
  return FALSE;
}

/** Call the correct function to execute the next instruction of the process
 *  If there is no instruction to execute, call:
 *   log_msg("Error: No instruction to execute");
 *  After successful execution, call:
 *   log_running((pcb, "Running");
 *   log_queue((readyq.first, "Ready");
 *   log_queue((waitingq.first, "Waiting");
 *   log_queue((terminatedq.first, "Termianted");
 *   log_msg("\n");
 **/
void execute_instr(pcb_t *pcb)
{
  omp_set_nest_lock(&i_lock);

  // Check if there is an instruction to execute
  if (pcb->next_instruction == NULL)
  {
    log_msg("Error: No instruction to execute ");
#ifdef DEBUG
    fprintf(stderr, "No instruction in thread %d \n", omp_get_thread_num());
#endif
    omp_unset_nest_lock(&i_lock);
    return;
  }
  // Handle resource request instruction
  else if (pcb->next_instruction->type == REQ_OP)
  {
    request_resource(pcb);
  }
  // Handle resource release instruction
  else
  {
    release_resource(pcb);
  }
  // Log state of all queues after execution
  log_running(pcb, "Running");
  log_queue(readyq.first, "Ready");
  log_queue(waitingq.first, "Waiting");
  log_queue(terminatedq.first, "Terminated");
  log_msg("\n");
  omp_unset_nest_lock(&i_lock);
}

/**
 * @brief Handle the request resource instruction
 *
 * If the resource could not be acquired move the process to the waiting queue
 * If the resource was successfully acquired call:
 *  log_request_acquired(cur_pcb->process->name, instr->resource_name);
 *  log_avail_resources(system_resources);
 *  log_msg("\n");
 */
void request_resource(pcb_t *cur_pcb)
{
  omp_set_nest_lock(&r_lock);

#ifdef DEBUG_RESOURCE
  fprintf(stderr, "Thread %d: Process is requesting resource\n", omp_get_thread_num());
#endif

  resource_t *resource = system_resources;
  bool resource_found = FALSE;
  // Iterate through resource to find requested resource
  while (resource != NULL)
  {
    // Check if current resource matches the requested resource
    if (strcmp(resource->name, cur_pcb->next_instruction->resource_name) == 0)
    {
      // If resource is available and not allocated, assign it the the requestion process
      if (resource->allocated == NULL)
      {
        resource->allocated = cur_pcb;
        log_request_acquired(cur_pcb->process->name, cur_pcb->next_instruction->resource_name);
        log_avail_resources(system_resources);
        log_msg("\n");
        resource_found = TRUE;
        break;
      }
    }
    resource = resource->next; // Move to next resource
  }
  // if resource doesnt exist in system the error has to be logged
  if (!resource_found)
  {
    // Process should be moved to waiting if resource doesnt exist.
    enqueue_pcb(cur_pcb, &waitingq, WAITING);
  }
  omp_unset_nest_lock(&r_lock);
}

/**
 * @brief Execute the release instruction for the process
 *  Update the allocated field
 *  Find a process that is waiting for a resource with the same name and move it to the ready queue
 *
 * If the release was successful, call:
 *  log_release_released(pcb->process->name, resource_name);
 *  log_avail_resources(system_resources);
 *  log_msg("\n");
 * If the release was not successful, call:
 *  log_release_error(pcb->process->name, resource_name);
 *
 */
void release_resource(pcb_t *pcb)
{
  omp_set_nest_lock(&r_lock);

  resource_t *resource = system_resources;
  bool resource_found = FALSE;

#ifdef DEBUG
  fprintf(stderr, "Test failed on thread %d\n", omp_get_thread_num());
#endif
  // Find resource allocated to process
  while (resource != NULL)
  {
    // Check if the resource currently allocated to the process
    if (strcmp(resource->name, pcb->next_instruction->resource_name) == 0)
    {
      // Make sure that this process owns the resource before releasing it
      if (resource->allocated == pcb)
      {
        // Releasing the resource
        resource->allocated = NULL;
        log_release_released(pcb->process->name, resource->name);
        log_avail_resources(system_resources);
        log_msg("\n");
        // Check to see if process is waiting
        omp_set_nest_lock(&q_locks[1]);
        pcb_t *waiting_process = waitingq.first;
        while (waiting_process != NULL)
        {
          // Find the first process waiting for this resource
          if (strcmp(waiting_process->next_instruction->resource_name, resource->name) == 0)
          {
            // Move waiting process to the ready queue
            pcb_t *removed_process = remove_pcb(&waitingq, waiting_process);
            omp_set_nest_lock(&q_locks[0]);
            enqueue_pcb(removed_process, &readyq, READY);
            omp_unset_nest_lock(&q_locks[0]);
            break;
          }
          waiting_process = waiting_process->next;
        }
        omp_unset_nest_lock(&q_locks[1]);
        resource_found = TRUE;
        break;
      }
    }
    resource = resource->next; // Move to next resource
  }
  // Resource not found
  if (!resource_found)
  {
    log_release_error(pcb->process->name, pcb->next_instruction->resource_name);
#ifdef DEBUG
    fprintf(stderr, "Release failed thread %d\n", omp_get_thread_num());
#endif
  }
  omp_unset_nest_lock(&r_lock);
}

/**
 * @brief Enqueue process <code>pcb</code> to <code>queue</code>
 * Log the enqueue operation appropiately, depending on <code>status</code>
   log_request_ready(pcb->process->name);
   log_request_waiting(pcb->process->name, pcb->next_instruction->resource_name);
   log_terminated(pcb->process->name);
 */
void enqueue_pcb(pcb_t *pcb, pcb_queue_t *queue, int status)
{
  // Enqueue process to the specified queue
  omp_set_nest_lock(&q_locks[queue->lock_index]);

  // If the queue is empty, set the PCB as the first and last element
  if (queue->last == NULL)
  {
    queue->first = pcb;
    queue->last = pcb;
  }
  else
  {
    // Append the PCB to the end of the queue
    queue->last->next = pcb;
    queue->last = pcb;
  }
  // Update the process state and ensure it is properly linked
  pcb->state = status;
  pcb->next = NULL;

  // Logging the process state change
  if (pcb->state == READY)
  {
    log_request_ready(pcb->process->name);
  }
  else if (pcb->state == WAITING)
  {
    log_request_waiting(pcb->process->name, pcb->next_instruction->resource_name);
  }
  else if (pcb->state == TERMINATED)
  {
    log_terminated(pcb->process->name);
#ifdef DEBUG
    fprintf(stderr, "Process %s thread %d\n", pcb->process->name, omp_get_thread_num());
#endif
  }
  omp_unset_nest_lock(&q_locks[queue->lock_index]);
}

/** Dequeue process pcb from queue <code>queue</code>. */
pcb_t *dequeue_pcb(pcb_queue_t *queue)
{
  omp_set_nest_lock(&q_locks[queue->lock_index]);
  pcb_t *pcb = NULL;

  if (queue->first != NULL) // Check if there is something in the queue
  {
    pcb = queue->first;       // Get the first process
    queue->first = pcb->next; // Move the front pointer to the next process
    if (queue->first == NULL)
    {
      queue->last = NULL; // If the queue is now empty, reset last pointer
    }
    pcb->next = NULL; // Detach the dequeue process from the queue
  }
  omp_unset_nest_lock(&q_locks[queue->lock_index]);
  return pcb;
}

/**
 * Remove a specified process from queue
 * Returns the removed process is found, otherwise return NULL
 */
pcb_t *remove_pcb(pcb_queue_t *queue, pcb_t *pcb)
{
  bool_t found;
  omp_set_nest_lock(&q_locks[queue->lock_index]);

  // If the queue is empty, return NULL
  if (queue->first == NULL)
  {
    return NULL;
  }

  // Find prev process in queue
  pcb_t *prev = get_prev(queue, pcb, &found);
  // If process is not found in queue, return NULL
  if (found == FALSE)
  {
    return NULL;
  }
  // If process is at front of the queue
  if (prev == NULL)
  {
    queue->first = queue->first->next;
    if (queue->first == NULL) // If queue is empty after removal, reset last pointer
    {
      queue->last = NULL;
    }
  }
  // If process is at the end of queue
  else if (queue->last == pcb)
  {
    queue->last = prev;
    prev->next = NULL;
  }
  // Process is in the middle of the queue
  else
  {
    prev->next = pcb->next;
  }
  pcb->next = NULL;
  omp_unset_nest_lock(&q_locks[queue->lock_index]);
  return pcb;
}

/**
 * @brief Removes a process form a queue without needing a lock,
 * returns the removal process of NULL if not found
 */
pcb_t *remove_pcb_unlock(pcb_queue_t *queue, pcb_t *pcb)
{
  bool_t found;
  pcb_t *prev = get_prev(queue, pcb, &found);
  // If process not found in queue, return null
  if (found == FALSE)
  {
    return NULL;
  }

  // If the process is at the front of the queue
  if (prev == NULL)
  {
    queue->first = queue->first->next;
    if (queue->first == NULL)
    {
      queue->last = NULL; // Queue is empty
    }
  }
  // If the process is at the end of the queue
  else if (queue->last == pcb)
  {
    queue->last = prev;
    prev->next = NULL;
  }
  // If the process is in the middle of the queue
  else
  {
    prev->next = pcb->next;
  }
  pcb->next = NULL;
  return pcb;
}

/**
 * @brief Finds the prev process in a queue before a given process
 * Sets *found to TRUE if the process is found, otherwise sets *found to FALSE
 * Returns the previous process, or NULL if process is the first element or not found.
 */
pcb_t *get_prev(pcb_queue_t *queue, pcb_t *pcb, bool_t *found)
{
  pcb_t *p = queue->first; // Previous node
  pcb_t *q = queue->first; // Current node
  // If queue is empty, return not found
  if (p == NULL)
  {
    *found = FALSE;
    return NULL;
  }
  // If the first process is the one we looking for, no previous exists
  if (p == pcb)
  {
    *found = TRUE;
    return NULL;
  }
  q = q->next; // Move to next process
  // Traverse the queue to find the process
  while (q != pcb && q != NULL)
  {
    p = q;
    q = q->next;
  }
  // If found, return the previous process
  if (p != NULL)
  {
    *found = TRUE;
  }
  else
  {
    *found = FALSE;
  }

  return p;
}

/**
 * @brief Finds all blocked processes in the waiting queue
 */
pcb_queue_t get_block_processes()
{
  pcb_t *p = waitingq.first; // Start from first process in waiting queue
  pcb_queue_t blocked_q;
  blocked_q.first = NULL;
  blocked_q.last = NULL;
  pcb_t *q = NULL;

  // Iterate through the waiting queue
  while (p != NULL)
  {
    char *resource_name = p->next_instruction->resource_name;
    resource_t *r = system_resources;
    bool_t found = FALSE;
    // Check if resource still exists in system_resources
    while (r != NULL)
    {
      if (strcmp(resource_name, r->name) == 0)
      {
        found = TRUE;
        break;
      }
      r = r->next;
    }
    q = p->next; // Store next process

    // If resource does not exist, move process to the blocked queue
    if (found == FALSE)
    {
      pcb_t *blocked = remove_pcb(&waitingq, p);

      // If blocked queue is empty, set first process
      if (blocked_q.first == NULL)
      {
        blocked_q.first = blocked;
        blocked_q.last = blocked;
      }
      else
      {
        blocked_q.last->next = blocked;
        blocked_q.last = blocked;
      }
    }
    p = q; // Move to next process in waiting queue
  }
  return blocked_q; // Return queue of blocked processes
}

/**
 * @brief detect deadlock
 * If deadlock is detected, call
 *  log_deadlock_detected();
 */
void detect_deadlock(void)
{
  // If there are no waiting processes, there is no deadlock
  if (waitingq.first == NULL)
  {
#ifdef DEBUG
    fprintf(stderr, "No waiting processes, no deadlock possible, thread %d\n", omp_get_thread_num());
#endif

    return;
  }
  // Get all processes that are blocked due to unavailable resources
  pcb_queue_t blocked_q = get_block_processes();

  // If there are still processes waiting, check for a cycle
  if (waitingq.first != NULL)
  {
    // Printing Cycle
    pcb_t *curr = waitingq.first;
    log_deadlock_detected(); // Log the detected deadlock
    bool_t first = TRUE;     // Used to track the first process in the cycle.
    pcb_t *prev = NULL;      // Store previous process to prevent infinite loops

    // Iterate through waiting processes to detect cycles
    while (curr != NULL && waitingq.first != NULL)
    {
      if (first)
      {
        first = FALSE;
        printf(" %s", curr->process->name); // Print first process in cycle
      }
      // Get the resource that the current process is waiting for
      resource_t *waiting_resource = get_waiting_resource(curr->next_instruction->resource_name);

      if (waiting_resource != NULL)
      {
        // Print the process that has the requested resource
        printf("->%s", waiting_resource->allocated->process->name);

        // Remove current process from the waiting queue
        remove_pcb(&waitingq, curr);

        // Preventing infinite loops
        if (waiting_resource->allocated == prev)
        {
          prev = curr;
          curr = dequeue_pcb(&waitingq);
          first = TRUE;
          continue;
        }
        prev = curr;
        curr = waiting_resource->allocated;
      }
      else
      {
        break;
      }
    }
    printf("\n"); // Print cycle
  }

  // If there are blocked processes, and log
  if (blocked_q.first != NULL)
  {
    log_blocked_procs();
    printf(" ");

    // Iterate through blocked processes and print their names
    pcb_t *b = blocked_q.first;
    while (b != NULL)
    {
      printf("%s ", b->process->name);
      b = b->next;
    }
    printf("\n");
  }
  return;
}

/**
 * @brief Gets the resource that a process is waiting for
 *
 * Searches through system resources to find a resource that is currently
 * allocated and matches the given resource name
 */
resource_t *get_waiting_resource(char *resource_name)
{
  resource_t *r = system_resources;

  // Go through list of system resources
  while (r != NULL)
  {
    if (r->allocated != NULL && strcmp(resource_name, r->name) == 0)
    {
      return r;
    }
    r = r->next; // Move to next resource
  }
  return NULL; // Resource not found
}

/** @brief Deallocate the queues */
void free_manager(void)
{
  log_queue(readyq.first, "Ready");
  log_queue(waitingq.first, "Waiting");
  log_queue(terminatedq.first, "Terminated");

#ifdef DEBUG_MNGR
  printf("\nFreeing the queues...\n");
#endif
  dealloc_pcbs(readyq.first);
  dealloc_pcbs(waitingq.first);
  dealloc_pcbs(terminatedq.first);
}

/** @brief Retrieve the number of threads to create from the list of arguments */
int get_num_threads(int num_args, char **argv)
{
  if (num_args > 1)
    return atoi(argv[1]);
  else
    return 1;
}

/** @brief Retrieve the name of a process file or the codename "generate" from the list of arguments */
char *get_data(int num_args, char **argv)
{
  char *data_origin = "generate";
  if (num_args > 2)
    return argv[2];
  else
    return data_origin;
}

/** @brief Retrieve the scheduler algorithm type from the list of arguments */
int get_algo(int num_args, char **argv)
{
  if (num_args > 3)
    return atoi(argv[3]);
  else
    return 1;
}

/** @brief Retrieve the time quantum from the list of arguments */
int get_time_quantum(int num_args, char **argv)
{
  if (num_args > 4)
    return atoi(argv[4]);
  else
    return 1;
}

/** @brief Print the arguments of the program */
void print_args(int num_thr, char *data, int sched, int tq)
{
  printf("Arguments: num_threads = %d, data = %s, scheduler = %s, time quantum = %d\n", num_thr, data, (sched == 0) ? "priority" : (sched == 1) ? "RR"
                                                                                                                                                : "FCFS",
         tq);
}
