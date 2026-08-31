/* 
* Promela model of the MPI master and worker move evaluation in my_player.c
*/ 

/* comm_sz is 4*/ 
#ifndef NUM_WORKERS
#define NUM_WORKERS 3
#endif

#ifndef MAX_MOVES
#define MAX_MOVES 4
#endif

#ifndef MAX_ROUNDS
#define MAX_ROUNDS 2
#endif

/* Abstract score domain.
* ALPHA_MIN -> INT_MIN
* NO_EVAL -> INT_MAX 
*/ 
#define ALPHA_MIN 0
#define SCORE_MAX 3
#define NO_EVAL 9

/* Search depth handed out by the master*/ 
#define DEPTH_FULL 1
#define DEPTH_REDUCED 0

/* The evaluation of each move,fixed by init before a round*/ 
byte true_score[MAX_MOVES + 1];

/* Master state,mirroring the locals of execute_master*/ 
short my_colour;// - 1 signals termination
byte  round_no;
byte  num_moves;// move_stack.size after load_round_moves
byte  moves_left;// what remains on the stack
byte  master_alpha;
byte  master_depth;
byte  best_move;// 0 stands for max_move == - 1
byte  best_score;
bool round_done;// set once a round's collection loop has finished
byte best_true;// the largest true_score of this round's moves
byte workers_done;// how many workers have left run_worker

/* Tags -> my_player.c,message types -> comms.h*/ 
mtype = {
	MOVE_TAG,NO_WORK_TAG,
	GENERATE_MOVE,PLAY_MOVE,MATCH_RESET,GAME_TERMINATION,
	UNKNOWN,RECV_FAILED,CLIENT_DISCONNECTED
};

/* The two collectives,one channel per worker.*/ 
chan bcast_colour[NUM_WORKERS + 1] = [1] of { short };// bcast_colour carries my_colour,or - 1 to signal termination.
chan bcast_board[NUM_WORKERS + 1] = [1] of { byte };// bcast_board carries the round number instead of 64 board ints.
chan to_worker[NUM_WORKERS + 1] = [1] of { mtype,byte,byte,byte };// Master -> worker: tag,move,alpha,depth
chan to_master = [NUM_WORKERS] of { byte,byte,byte,byte };// Worker -> master: rank,move,score,alpha.
chan from_referee = [1] of { mtype };// Referee -> master

/* Abstraction of minimax_pruning in my_player.c*/ 
inline evaluate_move(m,in_alpha,in_depth,out_score,out_alpha)
{
	if
		#ifndef NO_LEAK
		/* No legal moves at this node*/ 
	:: out_score = NO_EVAL
		#endif
		
		/* beta <= * alpha holds after the first child*/ 
	:: (in_alpha == NO_EVAL || in_depth == DEPTH_REDUCED) -> 
		select(out_score : 1 .. SCORE_MAX)
		
		/* Full search of an ordinary node.*/ 
	:: (in_alpha != NO_EVAL && in_depth == DEPTH_FULL) -> 
		out_score = true_score[m]
	fi;
	
	/* current_alpha = alpha*/ 
	if
	:: out_score > in_alpha -> out_alpha = out_score
	:: out_score <= in_alpha -> out_alpha = in_alpha
	fi
}

/* send_next_move,with pop_move folded in.*/ 
inline send_next_move(dest,alpha)
{
	if
	:: moves_left == 0 -> 
		to_worker[dest]!NO_WORK_TAG,0,alpha,master_depth
	:: moves_left > 0 -> 
		to_worker[dest]!MOVE_TAG,moves_left,alpha,master_depth;
		moves_left--
	fi
}

/* MPI_Bcast(&end_game,...) with end_game = - 1.
* One broadcast only,not two: the worker breaks out before the board
* broadcast,so the collective sequences still match.
*/ 
inline bcast_terminate()
{
	i = 1;
	do
	:: i > NUM_WORKERS -> break
	:: i <= NUM_WORKERS -> bcast_colour[i]!- 1;i++
	od
}

/* 
* Execute the master process. Does this by sending out moves to workers,then 
* collecting the results. 
*/ 
inline execute_master()
{
	/* MPI_Bcast(&my_colour) then MPI_Bcast(board),to every worker*/ 
	round_done = false;
	i = 1;
	do
	:: i > NUM_WORKERS -> break
	:: i <= NUM_WORKERS -> 
		bcast_colour[i]!my_colour;
		bcast_board[i]!round_no;
		i++
	od;
	
	/* load_round_moves. The board is gone,so the number of legal moves
	this round is chosen nondeterministically. 0 -> pass.*/ 
	select(num_moves : 0 .. MAX_MOVES);
	moves_left = num_moves;
	best_true = 0;
	
	/* Fix genuine evaluation of each move for this round. Verification
	state only,so that a property can name the move that should win.*/ 
	i = 1;
	do
	:: i > num_moves -> break
	:: i <= num_moves -> 
		select(tmp_score : 1 .. SCORE_MAX);
		true_score[i] = tmp_score;
		if
		:: tmp_score > best_true -> best_true = tmp_score
		:: tmp_score <= best_true -> skip
		fi;
		i++
	od;
	
	master_alpha = ALPHA_MIN;
	master_depth = DEPTH_FULL;
	
	/* send_init_moves: one message per worker,alpha fixed at INT_MIN*/ 
	i = 1;
	do
	:: i > NUM_WORKERS -> break
	:: i <= NUM_WORKERS -> send_next_move(i,ALPHA_MIN);i++
	od;
	
	results = 0;
	best_move = 0;
	best_score = ALPHA_MIN;
	
	do
	:: results >= num_moves -> break
	:: results < num_moves -> 
		/* The turn clock can pass cut_off at any point in round,after
		which every remaining move goes out at the reduced depth.*/ 
		#ifndef NO_TIME_CUTOFF
		if
		:: master_depth = DEPTH_REDUCED
		:: skip
		fi;
		#endif
		
		to_master?r_rank,r_move,r_score,r_alpha;
		
		if
		:: r_alpha > master_alpha -> master_alpha = r_alpha
		:: r_alpha <= master_alpha -> skip
		fi;
		
		send_next_move(r_rank,master_alpha);
		
		if
		:: (r_score > best_score || best_move == 0) -> 
			best_score = r_score;
			best_move = r_move
		:: !(r_score > best_score || best_move == 0) -> skip
		fi;
		
		results++
	od;
	
	round_done = true
}

/* Worker process. Each worker receives a colour,then a round number,
* and then enters the main evaluation loop.
*/ 
proctype Worker(byte rank)
{
	short w_colour;
	byte  round;
	mtype tag;
	byte  move,in_alpha,in_depth;
	byte  score,current_alpha;
	
	do
	:: bcast_colour[rank]?w_colour;
		
		if
		:: w_colour < 0 -> break
		:: w_colour >= 0 -> skip
		fi;
		
		bcast_board[rank]?round;
		
		current_alpha = ALPHA_MIN;
		
		do
		:: to_worker[rank]?tag,move,in_alpha,in_depth;
			if
			:: tag == NO_WORK_TAG -> break
			:: tag == MOVE_TAG -> 
				evaluate_move(move,in_alpha,in_depth,score,current_alpha);
				to_master!rank,move,score,current_alpha
			fi
		od
	od;
	
	workers_done++
}

/* run_master. The referee message loop that drives everything else.*/ 
proctype Master()
{
	byte i,tmp_score,results;
	byte r_rank,r_move,r_score,r_alpha;
	mtype msg_type;
	bool running = true;
	
	my_colour = 0;
	round_no = 0;
	
	do
	:: !running -> break
	:: running -> 
		from_referee?msg_type;
		if
		:: msg_type == GENERATE_MOVE -> 
			round_no++;
			execute_master()
			
		:: msg_type == PLAY_MOVE -> skip
			
		:: msg_type == MATCH_RESET -> 
			my_colour = (my_colour + 1) % 2
			
		:: msg_type == GAME_TERMINATION -> 
			running = false;
			bcast_terminate()
			
		:: msg_type == UNKNOWN -> 
			running = false;
			bcast_terminate()
			
			/* DEFECT. run_master has no branch for these two,so running
			stays 1,the loop repeats,and no termination broadcast is
			ever issued. Modelled as skip,which is exactly what the
			C if - chain does with them.*/ 
		:: (msg_type == RECV_FAILED || msg_type == CLIENT_DISCONNECTED) -> skip
		fi
	od
}

/* The Ingenious Framework referee,modelled as an environment process.
* Promela models must be closed,so the environment has to be modelled
* too. budget bounds the number of ordinary messages so that every run
* terminates;the four terminal messages each end the session.
*/ 
proctype Referee()
{
	byte budget = MAX_ROUNDS;
	
	do
	:: from_referee!GAME_TERMINATION;break
	:: from_referee!UNKNOWN;break
	:: from_referee!RECV_FAILED;break
	:: from_referee!CLIENT_DISCONNECTED;break
	:: budget > 0 -> 
		budget--;
		if
		:: from_referee!GENERATE_MOVE
		:: from_referee!PLAY_MOVE
		:: from_referee!MATCH_RESET
		fi
	od
}

init {
	byte k = 1;
	
	atomic {
		do
		:: k > NUM_WORKERS -> break
		:: k <= NUM_WORKERS -> run Worker(k);k++
		od;
		run Master();
		run Referee()
	}
}

/* NO_LEAK and NO_TIME_CUTOFF are fault injection switches. They are not
* part of the model of my_player.c. Each one removes one of the two
* independent causes of best_is_maximal failing,so that verification can
* establish which cause is responsible rather than only that a fault
* exists. best_is_maximal holds only when both are defined.
*/ 

/* The master finalised a round on a score that is not an evaluation at
* all,but the INT_MAX that minimax uses to initialise minEval.*/ 
#define chose_no_eval (round_done && best_score == NO_EVAL)
ltl no_eval_chosen  { []!chose_no_eval }

/* The move the master played really was the best available one.*/ 
#define chose_maximal (true_score[best_move] == best_true)
ltl best_is_maximal { [] ((round_done && num_moves > 0) -> chose_maximal) }

/* Every worker has left run_worker and can reach MPI_Finalize.*/ 
#define all_home (workers_done == NUM_WORKERS)

ltl no_lost_results { [] (round_done -> len(to_master) == 0) }
ltl workers_finish  { <> all_home }