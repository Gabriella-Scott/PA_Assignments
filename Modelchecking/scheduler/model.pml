/* Configuration*/ 
#define NUM_PROCS 3
#define NUM_WORKERS 2
#define NUM_RESOURCES 2
#define MAX_INSTR 3

/* Process states*/ 
#define READY 0
#define RUNNING 1
#define WAITING 2
#define TERMINATED 3

/* Shared state*/ 
byte pcb_state[NUM_PROCS + 1];
byte instr_count[NUM_PROCS + 1];
byte waiting_for[NUM_PROCS + 1];
byte resource_owner[NUM_RESOURCES + 1];
byte executing[NUM_PROCS + 1];

chan ready_q = [NUM_PROCS] of { byte };
chan waiting_q = [NUM_PROCS] of { byte,byte };

init {// like main
	byte i = 1;
	do
	:: i <= NUM_PROCS -> 
		pcb_state[i] = READY;// push i to ready queue
		ready_q!i;// send i to ready queue
		i++
	:: else -> break
	od;
}

/* Termination check: models terminate()
* With dynamic arrival omitted,all_processes_loaded is always
* true,so this simplifies to: ready queue empty AND every
* process is either WAITING or TERMINATED. w and t are counted
* by scanning pcb_state[],which is the direct equivalent of
* queue_count(&waitingq) and queue_count(&terminatedq),since
* queue membership and pcb_state[] track the same thing here.
*/ 
inline check_terminate(done) {
	byte w = 0;
	byte t = 0;
	byte k = 1;
	do
	:: k <= NUM_PROCS -> 
		if
		:: pcb_state[k] == WAITING -> w++
		:: pcb_state[k] == TERMINATED -> t++
		:: else -> skip
		fi;
		k++
	:: else -> break
	od;
	done = (empty(ready_q) && (w + t == NUM_PROCS))
}

/* request_resource: models request_resource()
* If the resource is free,the process takes it and keeps
* running. Otherwise the process is put on the waiting queue
* and marked WAITING.
* 
* Note that "not free" here includes the case where p already
* owns r. That is faithful to manager.c: the C code only
* checks resource -> allocated == NULL,so a process that
* re - requests a resource it already holds is sent to the
* waiting queue to wait for itself. That is preserved
* deliberately rather than fixed,since it is exactly the kind
* of flaw the verification should expose.
*/ 
inline request_resource(p,r) {
	if
	:: resource_owner[r] == 0 -> 
		resource_owner[r] = p;
		waiting_for[p] = 0;
	:: resource_owner[r] != 0 -> 
		waiting_for[p] = r;
		pcb_state[p] = WAITING;
		waiting_q!p,r;
	fi
}

/* release_resource: models release_resource()
* Only the owner may release. On a successful release the
* resource is freed and the FIRST process waiting for that
* same resource is moved back to the ready queue. At most one
* waiter is woken per release,matching the break in the C
* scan of waitingq.
* 
* If the process does not own r,manager.c only logs an error
* and changes no state,so this models that as skip.
*/ 
inline release_resource(p,r) {
	byte woken;
	if
	:: resource_owner[r] == p -> 
		resource_owner[r] = 0;
		if
		:: waiting_q??[woken,eval(r)] -> // ??random receive
			waiting_q??woken,eval(r);
			waiting_for[woken] = 0;
			pcb_state[woken] = READY;
			ready_q!woken
		:: !(waiting_q??[woken,eval(r)]) -> skip
		fi
	:: resource_owner[r] != p -> skip
	fi
}

/* Worker process 
* Executes instructions for a PCB obtained from the ready queue.
*/ 
active [NUM_WORKERS] proctype Worker() {
	byte pcb_id;// proc id of PCB being executed
	bool got_one;// flag to indicate if a PCB was obtained from ready queue
	byte done;
	byte res;// resource requested by current instruction
	
	do
	:: true -> 
		
		atomic {
			if
			:: nempty(ready_q) -> 
				ready_q?pcb_id;// receive a PCB id from ready queue
				executing[pcb_id]++;// Increment executing count for the PCB
				got_one = true
			:: empty(ready_q) -> 
				got_one = false
			fi
		}
		
		if
		:: got_one -> 
			pcb_state[pcb_id] = RUNNING;
			
			/*----Instruction loop----
			* Models the "while (running_p -> state == RUNNING)"
			* loop in schedule_fcfs.
			*/ 
			do
			:: pcb_state[pcb_id] == RUNNING -> 
				if
				:: instr_count[pcb_id] == MAX_INSTR -> 
					/* models next_instruction == NULL*/ 
					pcb_state[pcb_id] = TERMINATED
				:: instr_count[pcb_id] < MAX_INSTR -> 
					/* Pick which resource this instruction
					* asks for. A process resuming after a
					* failed request re - runs the same
					* instruction,since manager.c breaks out
					* of the loop before advancing
					* next_instruction.
					*/ 
					if
					:: waiting_for[pcb_id] != 0 -> 
						res = waiting_for[pcb_id]
					:: waiting_for[pcb_id] == 0 -> 
						select(res : 1 .. NUM_RESOURCES)
					fi;
					
					/* execute_instr: branch on instruction type.
					* A process resuming a blocked request must
					* retry that request,it cannot switch to a
					* release,since next_instruction did not
					* advance. Otherwise the instruction type is
					* nondeterministic,which lets SPIN explore
					* every possible instruction stream.
					*/ 
					atomic {
						if
						:: waiting_for[pcb_id] != 0 -> 
							request_resource(pcb_id,res)
						:: waiting_for[pcb_id] == 0 -> 
							if
							:: request_resource(pcb_id,res)
							:: release_resource(pcb_id,res)
							fi
						fi
					}
					
					/* Advance to the next instruction only if
						* the process did not block,matching the
						* "if (state == WAITING) break;" placed
						* before the next_instruction advance.
						*/ 
						if
						:: pcb_state[pcb_id] == WAITING -> skip
						:: pcb_state[pcb_id] != WAITING -> 
							instr_count[pcb_id]++
						fi
					fi
				:: pcb_state[pcb_id] != RUNNING -> break
				od;
				executing[pcb_id]--// Decrement executing count for the PCB
			:: !got_one -> skip
				fi;		
// Termination check
				check_terminate(done);
				if 
				:: done -> break
				:: else -> skip
				fi
			od
		}
		
/* No two workers may execute the same pcb concurrently.*/ 
#define one_worker_per_pcb (executing[1] <= 1 && executing[2] <= 1 && executing[3] <= 1)
ltl pcb_mutex { [] one_worker_per_pcb }	
		