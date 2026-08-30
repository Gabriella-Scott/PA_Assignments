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

/* Tags -> my_player.c,message types -> comms.h*/ 
mtype = {
	MOVE_TAG,NO_WORK_TAG,
	GENERATE_MOVE,PLAY_MOVE,MATCH_RESET,GAME_TERMINATION,
	UNKNOWN,RECV_FAILED,CLIENT_DISCONNECTED
};

/* The two collectives,one channel per worker.
* bcast_colour carries my_colour,or - 1 to signal termination.
* bcast_board carries the round number instead of 64 board ints.
*/ 
chan bcast_colour[NUM_WORKERS + 1] = [1] of { short };
chan bcast_board[NUM_WORKERS + 1] = [1] of { byte };

/* Master -> worker: tag,move,alpha,depth*/ 
chan to_worker[NUM_WORKERS + 1] = [1] of { mtype,byte,byte,byte };

/* Worker -> master: rank,move,score,alpha.
* A single queue for all workers -> MPI_Iprobe and MPI_Recv
* with MPI_ANY_SOURCE.
*/ 
chan to_master = [NUM_WORKERS] of { byte,byte,byte,byte };

/* Abstraction of minimax_pruning in my_player.c*/ 
inline evaluate_move(m,in_alpha,in_depth,out_score,out_alpha)
{
	if
		/* No legal moves at this node*/ 
	:: out_score = NO_EVAL
		
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

/* 
* Execute the master process. Does this by sending out moves to workers,then 
* collecting the results. 
*/ 
inline execute_master()
{
	/* MPI_Bcast(&my_colour) then MPI_Bcast(board),to every worker*/ 
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
	
	/* Fix genuine evaluation of each move for this round. Verification
	state only,so that a property can name the move that should win.*/ 
	i = 1;
	do
	:: i > num_moves -> break
	:: i <= num_moves -> 
		select(tmp_score : 1 .. SCORE_MAX);
		true_score[i] = tmp_score;
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
		if
		:: master_depth = DEPTH_REDUCED
		:: skip
		fi;
		
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
	od
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
	od
}