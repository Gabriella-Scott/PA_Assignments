/* Configuration*/ 
#define NUM_PROCS 3
#define NUM_WORKERS 2
#define NUM_RESOURCES 2
#ifndef MAX_INSTR
#define MAX_INSTR 3
#endif
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
byte in_ready[NUM_PROCS + 1];

chan ready_q = [NUM_PROCS] of { byte };
chan waiting_q = [NUM_PROCS] of { byte,byte };

init {
	byte i = 1;
	do
	:: i <= NUM_PROCS -> 
		atomic {
			pcb_state[i] = READY;
			in_ready[i]++;
			ready_q!i
		};
		i++
	:: else -> break
	od;
}

/* Termination check: models terminate()*/ 
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

/* request_resource: models request_resource()*/ 
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

/* release_resource: models release_resource()*/ 
inline release_resource(p,r) {
	byte woken;
	if
	:: resource_owner[r] == p -> 
		resource_owner[r] = 0;
		if
		:: waiting_q??[woken,eval(r)] -> 
			waiting_q??woken,eval(r);
			waiting_for[woken] = 0;
			pcb_state[woken] = READY;
			in_ready[woken]++;
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
				ready_q?pcb_id;
				in_ready[pcb_id]--;
				executing[pcb_id]++;
				got_one = true
			:: empty(ready_q) -> 
				got_one = false
			fi
		}
		
		if
		:: got_one -> 
			pcb_state[pcb_id] = RUNNING;
			
			/* Instruction loop
			* Models: "while (running_p -> state == RUNNING)"
			*/ 
			do
			:: pcb_state[pcb_id] == RUNNING -> 
				if
				:: instr_count[pcb_id] == MAX_INSTR -> 
					/* models next_instruction == NULL*/ 
					pcb_state[pcb_id] = TERMINATED
				:: instr_count[pcb_id] < MAX_INSTR -> 
					/* Pick which resource this instruction asks for.*/ 
					if
					:: waiting_for[pcb_id] != 0 -> 
						res = waiting_for[pcb_id]
					:: waiting_for[pcb_id] == 0 -> 
						select(res : 1 .. NUM_RESOURCES)
					fi;
					
					/* execute_instr: branch on instruction type*/ 
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
						* the process did not block*/ 
						if
						:: pcb_state[pcb_id] == WAITING -> skip
						:: pcb_state[pcb_id] != WAITING -> 
							instr_count[pcb_id]++
						fi
					fi
				:: pcb_state[pcb_id] != RUNNING -> break
				od;
				executing[pcb_id]--// Decr executing count for the PCB
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
		
		/* No process may be waiting for a resource that it already owns.*/ 
		#define no_self_wait_p ( \
		(waiting_for[1] == 0 || resource_owner[waiting_for[1]] != 1) && \
		(waiting_for[2] == 0 || resource_owner[waiting_for[2]] != 2) && \
		(waiting_for[3] == 0 || resource_owner[waiting_for[3]] != 3))
	ltl no_self_wait { [] no_self_wait_p }
		
		/* A process marked WAITING must have a resource recorded that it
		* is waiting for.*/ 
		#define waiting_consistent_p ( \
		(pcb_state[1] != WAITING || waiting_for[1] != 0) && \
		(pcb_state[2] != WAITING || waiting_for[2] != 0) && \
		(pcb_state[3] != WAITING || waiting_for[3] != 0))
	ltl waiting_consistent { [] waiting_consistent_p }
		
		/* A PCB may not appear in the ready queue more than once at the
		* same time.*/ 
		#define no_dup_ready_p (in_ready[1] <= 1 && in_ready[2] <= 1 && in_ready[3] <= 1)
	ltl no_dup_ready { [] no_dup_ready_p }
		
		/* Every process eventually reaches TERMINATED*/ 
		#define all_done (pcb_state[1] == TERMINATED && pcb_state[2] == TERMINATED && pcb_state[3] == TERMINATED)
	ltl all_terminate { <> all_done }
		
		/* A process placed on the ready queue is eventually dispatched.*/ 
	ltl ready_dispatched { [] ((pcb_state[1] == READY) -> <> (pcb_state[1] == RUNNING)) }
		