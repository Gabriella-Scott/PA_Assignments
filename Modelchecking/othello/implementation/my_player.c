/************************************************************************
 *
 *  This is a skeleton to guide development of Othello engines that is intended
 *  to be used with the Ingenious Framework.
 *
 *  The skeleton has a simple random strategy that can be used as a starting
 *  point. The master thread (rank 0) currently runs the random strategy and
 *  handles the communication with the referee, and the worker threads currently
 *  do nothing. Some form of backtracking algorithm, minimax, negamax,
 *  alpha-beta pruning etc. in parallel should be implemented.
 *
 *  Therfore, skeleton code provided can be modified and altered to implement
 *  different strategies for the Othello game. However, the flow of
 *  communication with the referee, relies on the Ingenious Framework and should
 *  not be changed.
 *
 *  Each engine is wrapped in a process which communicates with the referee, by
 *  sending and receiving messages via the server hosted by the Ingenious
 *  Framework.
 *
 *  The communication enumes are defined in comms.h and are as follows:
 *      - GENERATE_MOVE: Referee is asking for a move to be made.
 *      - PLAY_MOVE: Referee is forwarding the opponent's move. For this engine
 *        to update the board state.
 *     - MATCH_RESET: Referee is asking for the board to be reset. Likely, for
 *        another game.
 *     - GAME_TERMINATION: Referee is asking for the game to be terminated.
 *
 *  IMPORTANT NOTE FOR DEBBUGING:
 *      - Print statements to stdout will most likely not be visible when
 *        running the engine with the Ingenious Framework. Therefore, it is
 *        recommended to print to a log file instead. The pointer to the log
 *        file is passed to the initialise_master function.
 *
 ************************************************************************/
#include "comms.h"
#include <arpa/inet.h>
#include <mpi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#include <stdarg.h>

#define BOARD_SIZE 8
#define EMPTY -1
#define BLACK 0
#define WHITE 1
#define BOARD_CAP 64

#define MAX_MOVES 64

#define MOVE_TAG 5
#define NO_WORK_TAG 8

// Enable debug
#define DEBUG 0

#ifndef DEBUG
#define DEBUG_PRINT(f, ...) debug_log(f, __VA_ARGS__)
#else
#define DEBUG_PRINT(F, ...) ((void)0)
#endif

// Using struct to send move and score together
struct
{
    int move;
    int score;
} local_best, global_best;

typedef struct Node Node;

// Struct for the move stack implementation
struct Node
{
    int move;
    Node *next;
};

// Struct for managing available moves
typedef struct
{
    Node *head;
    int size;
} Stack;

int position_weights[BOARD_SIZE][BOARD_SIZE] = {
    {20, -3, 8, 6, 6, 8, -3, 20},     // Adjacent horizontally/vertically to corners are risky
    {-3, -7, -4, -4, -4, -4, -7, -3}, // Adjacent diagonally to corners, very dangerous as they often give your opponent access to corners
    {8, -4, 7, 4, 4, 7, -4, 8},       // 6-8 harder to flip than center pieces
    {6, -4, 4, 0, 0, 4, -4, 6},
    {6, -4, 4, 0, 0, 4, -4, 6},
    {8, -4, 7, 4, 4, 7, -4, 8},
    {-3, -7, -4, -4, -4, -4, -7, -3},
    {20, -3, 8, 6, 6, 8, -3, 20}};

const char *PLAYER_NAME_LOG = "my_player.log";
const char *WORKER_NAME_LOG = "my_player_worker_x_.log";

void run_master(int, char *[]); // Process 0
void execute_master(int my_colour, int comm_sz, int time_limit);
void serial_master(int my_colour, int time_limit);

int initialise_master(int, char *[], int *, int *, FILE **);
int initialise_worker(int rank, int my_colour, FILE **fp);
void setup_board_weights();

void initialise_board(void);
void free_board(void);
void print_board(FILE *);
void reset_board(FILE *);

void run_worker(int); // Rest of processes
int get_opponent(int my_colour);
void divide_work(int my_colour, int *moves, int *num_moves);
int evaluate(int my_colour);
void check_moves(int *moves, int num_moves, int my_colour, int rank, double time_limit, FILE *fp);
int minimax(int move, int depth, int *alpha, int beta, int maxPlayer, int my_colour);
int minimax_pruning(int move, int my_colour, FILE *fp, int alpha, int depht);
int maximum(int m1, int m2);
int minimum(int m1, int m2);
int game_over();
void copy_board(int *copy);
void restore_board(int *copy);

int random_strategy(int, FILE *);
void legal_moves(int *, int *, int);
int check_direction(int, int, int, int, int, int);
void make_move(int, int);
void flip_direction(int, int, int, int, int);
void load_round_moves(int my_colour);
void send_init_moves(int my_colour, int comm_sz, int depth);
void send_next_move(int process_rank, int alpha, int depth);
int pop_move();
int get_elapsed_time(time_t start_time);
int is_frontier(int row, int col);
int is_stable(int row, int col, int colour);
int count_stable_disks(int color);

// Global variables
int *board;
int *board_weights;
Stack move_stack;
FILE *master_fp;
int current_alpha; // Current alpha value for alpha-beta pruning, shared between functions

int main(int argc, char *argv[])
{
    int rank;

    if (argc != 5)
    {
        printf("Usage: %s <inetaddress> <port> <time_limit> <player_colour>\n",
               argv[0]);
        return 1;
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    /* each process initialises their own board */
    initialise_board();

    if (rank == 0)
    {
        run_master(argc, argv);
    }
    else
    {
        run_worker(rank);
    }

    free_board();

    MPI_Finalize();
    return 0;
}

/**
 * Runs the master process.
 *
 * @param argc command line argument count
 * @param argv command line argument vector
 */
void run_master(int argc, char *argv[])
{
    int msg_type, time_limit, my_colour, my_move, opp_move, running;
    char *move;
    int end_game = -1;
    int comm_sz;

    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    running = initialise_master(argc, argv, &time_limit, &my_colour, &master_fp);

    while (running)
    {

        msg_type = receive_message(&opp_move);
        if (msg_type == GENERATE_MOVE)
        { /* referee is asking for a move */
            if (comm_sz == 1)
            {
                serial_master(my_colour, time_limit);
            }
            else
            {
                execute_master(my_colour, comm_sz, time_limit);
            }
            my_move = global_best.move;

            if (my_move != -1)
            {
                make_move(my_move, my_colour);
                fprintf(master_fp, "\nPlacing piece in row: %d, column: %d\n",
                        my_move / BOARD_SIZE, my_move % BOARD_SIZE);
            }
            else
            {
                fprintf(master_fp, "\n Only move is to pass\n");
            }
            /* convert move to char */
            move = malloc(sizeof(char) * 10);
            sprintf(move, "%d\n", my_move);
            send_move(move);
            free(move);
        }
        else if (msg_type ==
                 PLAY_MOVE)
        { /* referee is forwarding opponents move */

            if (opp_move < 0)
            {
                fprintf(master_fp, "\nOpponent had no moves, therefore passed.");
                continue;
            }

            fprintf(master_fp, "\nOpponent placing piece in row: %d, column: %d\n",
                    opp_move / BOARD_SIZE, opp_move % BOARD_SIZE);

            make_move(opp_move, (my_colour + 1) % 2);
        }
        else if (msg_type == GAME_TERMINATION)
        {
            fprintf(master_fp, "Game terminated.\n");
            fflush(master_fp);
            running = 0;
            MPI_Bcast(&end_game, 1, MPI_INT, 0, MPI_COMM_WORLD);
        }
        else if (msg_type == MATCH_RESET)
        {
            fprintf(master_fp, "Match reset.\n");
            my_colour = (my_colour + 1) % 2;
            reset_board(master_fp);
        }
        else if (msg_type == UNKNOWN)
        {
            fprintf(master_fp, "Received unknown message type from referee.\n");
            fflush(master_fp);
            running = 0;
            MPI_Bcast(&end_game, 1, MPI_INT, 0, MPI_COMM_WORLD);
        }

        if (msg_type == GENERATE_MOVE || msg_type == PLAY_MOVE ||
            msg_type == MATCH_RESET)
        {
            print_board(master_fp);
            fprintf(master_fp, "message type: %d\n", msg_type);
            fflush(master_fp);
        };
    }
}

/**
 * Main function for master process to execute the parallel move evaluation strategy.
 * Coordinates worker processes, distributes moves for evaluation, collects results,
 * and selects the best move based on minimax scores.
 *
 * @param my_colour Colour of the current player
 * @param comm_sz Total processes
 * @param time_limit Time limit (milliseconds)
 */
void execute_master(int my_colour, int comm_sz, int time_limit)
{
    int results;
    int num_moves;
    MPI_Status status;
    int msg_avail;
    int process_move, process_score, process_rank;
    int *process_buffer = malloc(sizeof(int) * 3);
    int max_move, max_score;
    int alpha;
    int depth;
    time_t start_time = time(NULL);

    DEBUG_PRINT(master_fp, "Starting execute_master with colour %d, comm_sz %d, time_limit %d\n", my_colour, comm_sz, time_limit);

    // Broadcast board, colour to all worker processes
    MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(board, BOARD_CAP, MPI_INT, 0, MPI_COMM_WORLD);
    DEBUG_PRINT(master_fp, "Broadcasted color and board to workers\n");

    // Set up a move stack, with all legal moves for this round
    load_round_moves(my_colour);
    num_moves = move_stack.size;
    alpha = INT_MIN;
    depth = 11;

    DEBUG_PRINT(master_fp, "Found %d legal moves, initial depth: %d\n", num_moves, depth);

    if (num_moves == 1)
    {
        depth = 14;
    }
    else if (num_moves == 2)
    {
        depth = 13;
    }
    else if (num_moves == 3)
    {
        depth = 12;
    }

    // Initial distribution of moves to worker processes
    DEBUG_PRINT(master_fp, "Sending initial moves to workers");
    send_init_moves(my_colour, comm_sz, depth);

    // Receive and send moves until no more move left
    results = 0;
    max_move = -1;
    max_score = INT_MIN;

    int cut_off = time_limit - 2000; // Set a cutoff time to ensure we finish before the time limit
    if (cut_off < 0)
    {
        cut_off = 1000;
    }

    // Continue until all moves have been evaluated
    while (results < num_moves)
    {
        // Check if running out of time, reduce depth if needed
        int elapsed = get_elapsed_time(start_time);
        if (elapsed > cut_off)
        { // Using cutoff for, because of 4 second limit
            DEBUG_PRINT(master_fp, "Time cutoff reached (%d ms), reducing depth to 4", elapsed);
            depth = 4;
        }

        // Check for incoming messages from worker
        msg_avail = 0;
        MPI_Iprobe(MPI_ANY_SOURCE, MOVE_TAG, MPI_COMM_WORLD, &msg_avail, &status);
        if (msg_avail)
        {
            process_rank = status.MPI_SOURCE;
            MPI_Recv(process_buffer, 3, MPI_INT, process_rank, MOVE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            DEBUG_PRINT(master_fp, "Message received from process %d, with move %d and evaluation %d\n", process_rank, process_buffer[0], process_buffer[1]);

            fprintf(master_fp, "Received move %d, with the evaluation %d and the alpha %d from process %d\n", process_buffer[0], process_buffer[1], process_buffer[2], process_rank);
            fflush(master_fp);
            fprintf(master_fp, "Elapsed (%d seconds elapsed) time limit %d\n\n", elapsed, time_limit);
            fflush(master_fp);

            // Update global alpha value for pruning
            if (process_buffer[2] > alpha)
            {
                DEBUG_PRINT(master_fp, "Updating alpha from %d to %d\n", alpha, process_buffer[2]);
                alpha = process_buffer[2];
            }

            // Assign next move to the worker process
            send_next_move(process_rank, alpha, depth);

            // Update best move if this one is better
            process_score = process_buffer[1];
            process_move = process_buffer[0];
            if ((process_score > max_score) || (max_move == -1))
            {
                // Found a better move
                DEBUG_PRINT(master_fp, "New best move: %d (row: %d, col: %d) with score: %d (previous best: %d)",
                            process_move, process_move / BOARD_SIZE, process_move % BOARD_SIZE,
                            process_score, max_score);
                max_score = process_score;
                max_move = process_move;
            }
            results++;
        }
    }
    // Store best move
    global_best.move = max_move;
    global_best.score = max_score;
    fprintf(master_fp, "Final move %d with the score %d\n", max_move, max_score);
    DEBUG_PRINT(master_fp, "Storing move with process %d, with move %d and evaluation %d ", process_rank, process_buffer[0], process_buffer[1]);
    fflush(master_fp);
}

/**
 * Sends initial moves to worker processes to start the evaluation.
 *
 * @param my_colour Colour of the current player
 * @param comm_sz Total processes
 * @param depth Search depth (for minimax algo)
 */
void send_init_moves(int my_colour, int comm_sz, int depth)
{
    for (int process_rank = 1; process_rank < comm_sz; process_rank++)
    {
        DEBUG_PRINT(master_fp, "Sending initial move to worker %d", process_rank);
        send_next_move(process_rank, INT_MIN, depth);
    }
}

/**
 * Sends the next available move to a worker process for evaluation.
 * If no more moves are available, sends a NO_WORK_TAG message.
 *
 * @param process_rank Rank of the worker process
 * @param alpha Current alpha value for alpha-beta pruning
 * @param depth Search depth Search depth (for minimax algo)
 */
void send_next_move(int process_rank, int alpha, int depth)
{
    int *move = malloc(sizeof(int) * 3);
    move[0] = pop_move(); // Get next move from stack
    move[1] = alpha;      // Current alpha (for pruning)
    move[2] = depth;      // Search depth
    int tag;
    if (move[0] == -1)
    {
        tag = NO_WORK_TAG; // No more moves to evaluate
        DEBUG_PRINT(master_fp, "No more moves, sending NO_WORK_TAG to worker %d", process_rank);
    }
    else
    {
        tag = MOVE_TAG; // Send a move for evaluation
        DEBUG_PRINT(master_fp, "Sending move %d (row: %d, col: %d) to worker %d with alpha %d, depth %d",
                    move[0], move[0] / BOARD_SIZE, move[0] % BOARD_SIZE, process_rank, move[1], move[2]);
    }
    MPI_Send(move, 3, MPI_INT, process_rank, tag, MPI_COMM_WORLD);
    free(move);
}

/**
 * Loads all legal moves for the current player into the move stack.
 * The stack is used to distribute work among worker processes.
 *
 * @param my_colour Colour of current player
 */
void load_round_moves(int my_colour)
{
    int *moves;
    int num_moves;

    moves = malloc(sizeof(int) * MAX_MOVES);
    legal_moves(moves, &num_moves, my_colour);

    DEBUG_PRINT(master_fp, "Found %d legal moves for colour %d", num_moves, my_colour);

    // Initialise empty stack
    move_stack.head = NULL;
    move_stack.size = 0;

    // Push all legal moves onto stack
    for (int i = 0; i < num_moves; i++)
    {
        Node *node = malloc(sizeof(Node));
        node->move = moves[i];
        node->next = NULL;

        // Add node to front of stack
        if (move_stack.head == NULL)
        {
            move_stack.head = node;
        }
        else
        {
            node->next = move_stack.head;
            move_stack.head = node;
        }
        move_stack.size++;
        DEBUG_PRINT(master_fp, "Added move %d (row: %d, col: %d) to move stack", moves[i], moves[i] / BOARD_SIZE, moves[i] % BOARD_SIZE);
    }
    free(moves);
}

/**
 * Removes and returns the top move from the move stack.
 * Returns -1 if the stack is empty.
 *
 * @return popped move, or -1 if stack is empty
 */
int pop_move()
{
    Node *node;
    int move;
    if (move_stack.size == 0)
    {
        return -1;
    }

    // Remove node from front of stack
    node = move_stack.head;
    move_stack.head = node->next;

    move = node->move;
    free(node);
    move_stack.size--;

    DEBUG_PRINT(master_fp, "Popped move %d (row: %d, col: %d) from stack, %d moves remaining", move, move / BOARD_SIZE, move % BOARD_SIZE, move_stack.size);

    return move;
}

/**
 * Runs the worker process, evaluates moves using minimax algorithm.
 *
 * @param rank rank of the worker process
 */
void run_worker(int rank)
{
    int running, my_colour, score;
    FILE *fp;
    int first = 1;
    MPI_Status status;
    int *move_buffer = malloc(sizeof(int) * 3);

    running = 1;
    while (running)
    {
        // Receives the players colour from master
        MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (first)
        {
            initialise_worker(rank, my_colour, &fp);
            fprintf(fp, "\n");
            fflush(fp);
        }
        first = 0;

        // Check for game termination
        if (my_colour < 0)
        {
            fprintf(fp, "Game terminated.\n");
            DEBUG_PRINT(fp, "Worker %d received termination signal", rank);
            fflush(fp);
            break;
        }

        // Receive current board state
        MPI_Bcast(board, BOARD_CAP, MPI_INT, 0, MPI_COMM_WORLD);
        DEBUG_PRINT(fp, "Worker %d received board broadcast for colour %d", rank, my_colour);
        print_board(fp);
        fprintf(fp, "\n");

        // Evaluate the move and immediately return it to process 0, using the move tag
        current_alpha = INT_MIN; // Initialising alpha for this round
        DEBUG_PRINT(fp, "Worker %d initialized alpha to %d", rank, current_alpha);

        // Evaluate moves until no more work available
        while (running)
        {
            MPI_Recv(move_buffer, 3, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            if (status.MPI_TAG == NO_WORK_TAG)
            {
                DEBUG_PRINT(fp, "Worker %d received NO_WORK_TAG, breaking evaluation loop", rank);
                break; // No more moves
            }

            fprintf(fp, "Going to evaluate move %d, using alpha %d and depth %d\n", move_buffer[0], move_buffer[1], move_buffer[2]);
            DEBUG_PRINT(fp, "Worker %d evaluating move %d (row: %d, col: %d) with alpha %d and depth %d",
                        rank, move_buffer[0], move_buffer[0] / BOARD_SIZE, move_buffer[0] % BOARD_SIZE, move_buffer[1], move_buffer[2]);
            fflush(fp);

            // Evaluate move using minimax with alpha-beta pruning
            score = minimax_pruning(move_buffer[0], my_colour, fp, move_buffer[1], move_buffer[2]);
            DEBUG_PRINT(fp, "Worker %d finished evaluation, score: %d, updated alpha: %d", rank, score, current_alpha);

            // Send back results to master
            move_buffer[1] = score;         // Evaluation score
            move_buffer[2] = current_alpha; // Updated alpha value
            MPI_Send(move_buffer, 3, MPI_INT, 0, MOVE_TAG, MPI_COMM_WORLD);
        }
        fprintf(fp, "\n");
        fflush(fp);
    }
    free(move_buffer);
}

/** Returns opponents colour based on the players colour
 * @param my_colour The colour of the current player
 * @return The opponent's colour
 */
int get_opponent(int my_colour)
{
    return (my_colour + 1) % 2;
}

/**
 * Executes strategy for a single-process run
 * Gets legal moves and selects the best one using minimax.
 *
 * @param my_colour Colour of current player
 * @param time_limit Time limit (milliseconds)
 */
void serial_master(int my_colour, int time_limit)
{
    DEBUG_PRINT(master_fp, "Starting serial master for colour %d with time limit %d", my_colour, time_limit);
    int *moves = malloc(sizeof(int) * MAX_MOVES);
    int num_moves;
    divide_work(my_colour, moves, &num_moves);

    DEBUG_PRINT(master_fp, "Found %d legal moves in serial mode", num_moves);
    check_moves(moves, num_moves, my_colour, 0, time_limit, master_fp);
    free(moves);
}

/**
 * Divides work by getting all legal moves for current player.
 *
 * @param my_colour Colour of the current player
 * @param moves Array to store the legal moves
 * @param num_moves Pointer to store the number of legal moves
 */
void divide_work(int my_colour, int *moves, int *num_moves)
{
    legal_moves(moves, num_moves, my_colour);
    DEBUG_PRINT(master_fp, "Divided work: found %d legal moves for colour %d", *num_moves, my_colour);
}

/**
 * Tracks the elapsed time since the start of a move calculation.
 *
 * @param start_time The time when the move calculation started
 * @return The elapsed time in seconds as a double
 */
int get_elapsed_time(time_t start_time)
{
    time_t current_time = time(NULL);
    double elapsed_time = difftime(current_time, start_time);
    return (int)(elapsed_time * 1000);
}

/**
 * Evaluates all possible moves and selects the best one based on minimax scoring.
 * Uses MPI_Reduce to find the globally best move across all processes.
 *
 * @param moves Array of legal moves
 * @param num_moves Number of legal moves available
 * @param my_colour Colour of the current player
 * @param rank Process rank in MPI communication
 * @param time_limit Time limit in milliseconds
 * @param fp Log file pointer
 */
void check_moves(int *moves, int num_moves, int my_colour, int rank, double time_limit, FILE *fp)
{
    int max_score, move, score;
    time_t start_time = time(NULL);
    double time_per_move;

    DEBUG_PRINT(fp, "Process %d checking %d moves with time limit %f", rank, num_moves, time_limit);

    // Calculate time allocation per move
    if (num_moves > 0)
    {
        // Reserve time for overhead and final processing
        time_per_move = (time_limit * 0.8) / (double)num_moves;
        DEBUG_PRINT(fp, "Time per move: %f ms", time_per_move);
    }
    else
    {
        time_per_move = 0;
    }

    fprintf(fp, "Checking my moves as process %d\n", rank);
    fflush(fp);

    if (num_moves == 0)
    {
        // No legal moves to be played
        max_score = INT_MIN;
        move = -1;
        DEBUG_PRINT(fp, "No legal moves available, setting move to -1 (pass)");
    }
    else
    {
        // Initialse with first move as the best
        current_alpha = INT_MIN;
        DEBUG_PRINT(fp, "Evaluating first move: %d (row: %d, col: %d)", moves[0], moves[0] / BOARD_SIZE, moves[0] % BOARD_SIZE);
        max_score = minimax_pruning(moves[0], my_colour, fp, current_alpha, 5);
        move = moves[0];

        // Check each move tp find the one with highest score
        for (int i = 1; i < num_moves; i++)
        {
            // Recalculate remaining time
            int elapsed = get_elapsed_time(start_time);
            int remaining = time_limit - elapsed;

            // Skip further evaluation if almost out of time
            if (remaining < time_per_move)
            {
                fprintf(fp, "Almost out of time. Using best move found so far.\n");
                DEBUG_PRINT(fp, "Almost out of time. Stopping evaluation at move %d/%d", i, num_moves);
                fflush(fp);
                break;
            }

            // Evaluate this move
            score = minimax_pruning(moves[i], my_colour, fp, current_alpha, 5);
            DEBUG_PRINT(fp, "Move %d evaluation result: %d", moves[i], score);

            if (score > max_score)
            {
                // Found a better move
                max_score = score;
                move = moves[i];
            }
        }
    }

    // Store this process's best move and score
    global_best.move = move;
    global_best.score = max_score;

    // Total time spent for all moves
    double total_time = get_elapsed_time(start_time);
    fprintf(fp, "Process %d completed in %.2f seconds\n", rank, total_time);
}

/**
 * Implements the minimax algorithm with alpha-beta pruning for move evaluation.
 * Recursively explores the game tree to determine the optimal move.
 *
 * @param move The move to evaluate
 * @param depth Current depth in the game tree
 * @param alpha The best value for the maximizing player found so far
 * @param beta The best value for the minimizing player found so far
 * @param maxPlayer Boolean flag indicating if current level is maximising (1) or minimising (0)
 * @param my_colour Colour of the current player
 * @return The evaluation score for the given move
 */
int minimax(int move, int depth, int *alpha, int beta, int maxPlayer, int my_colour)
{
    int maxEval, minEval, eval;
    int number_of_moves;
    int *moves = malloc(sizeof(int) * MAX_MOVES);
    int *copy = malloc(sizeof(int) * BOARD_CAP);

    // Reaches max depth or game over (best cases)
    if (depth == 0 || game_over())
    {
        DEBUG_PRINT(master_fp, "Minimax reached end of search with depth %d, evaluation %d", depth, evaluate(my_colour));
        return evaluate(my_colour);
    }
    if (maxPlayer)
    {
        // Maximising the opponent's turn
        maxEval = INT_MIN;
        legal_moves(moves, &number_of_moves, get_opponent(my_colour));
        if (number_of_moves > 1)
        {
            number_of_moves = number_of_moves / 2;
        }

        // Try each legal move
        for (int i = 0; i < number_of_moves; i++)
        {
            copy_board(copy);
            make_move(moves[i], get_opponent(my_colour));
            // Recursively evaluate the path
            eval = minimax(moves[i], depth - 1, alpha, beta, 0, my_colour);
            // Restore board state
            restore_board(copy);
            maxEval = maximum(maxEval, eval);
            *alpha = maximum(*alpha, eval);
            // Alpha beta pruning
            if (beta <= *alpha)
            {
                break;
            }
        }
        free(moves);
        free(copy);
        return maxEval;
    }
    else
    {
        // Minimising player's turn
        minEval = INT_MAX;
        legal_moves(moves, &number_of_moves, my_colour);
        // Try each legal move
        for (int i = 0; i < number_of_moves; i++)
        {
            copy_board(copy);
            make_move(moves[i], my_colour);

            // Recursively calculate the path
            eval = minimax(moves[i], depth - 1, alpha, beta, 1, my_colour);

            // Restore board state
            restore_board(copy);

            minEval = minimum(minEval, eval);
            beta = minimum(beta, eval);
            // Alpha-beta pruning, stop evaluating if branch cannot improve result
            if (beta <= *alpha)
            {
                break;
            }
        }
        free(moves);
        free(copy);
        return minEval;
    }
}

/**
 * Wrapper function for the minimax algorithm that initializes the search.
 * Creates a temporary board copy, applies the move, runs minimax evaluation,
 * then restores the original board state.
 *
 * @param move The move to evaluate
 * @param my_colour Colour of the current player
 * @param fp Log file pointer
 * @param received_alpha Initial alpha value for pruning
 * @param depth Search depth for minimax algorithm
 * @return The evaluation score for the given move
 */
int minimax_pruning(int move, int my_colour, FILE *fp, int received_alpha, int depth)
{
    int score;
    // Allocate memory for copy of board
    int *copy = malloc(sizeof(int) * BOARD_CAP);
    int total_pieces = 0;
    int alpha = received_alpha;

    // Count total pieces to determine game phase
    for (int i = 0; i < BOARD_CAP; i++)
    {
        if (board[i] != EMPTY)
        {
            total_pieces++;
        }
    }
    // Save the current board state
    copy_board(copy);

    // Apply the move that is being evaluated
    make_move(move, my_colour);

    // Start minimax search
    score = minimax(move, depth, &alpha, INT_MAX, 0, my_colour);
    current_alpha = alpha;

    fprintf(fp, "Move %d has a score %d\n", move, score);
    fflush(fp);

    restore_board(copy);
    free(copy);
    return score;
}

/**
 * Returns the maximum of two integers.
 *
 * @param m1 First integer
 * @param m2 Second integer
 * @return The larger of the two integers
 */
int maximum(int m1, int m2)
{
    if (m1 > m2)
    {
        return m1;
    }
    else
    {
        return m2;
    }
}

/**
 * Returns the minimum of two integers.
 *
 * @param m1 First integer
 * @param m2 Second integer
 * @return The smaller of the two integers
 */
int minimum(int m1, int m2)
{
    if (m1 < m2)
    {
        return m1;
    }
    else
    {
        return m2;
    }
}

/**
 * Determines if the game has ended.
 * The game ends when both of the players has no moves left to make.
 *
 * @return 1 if the game is over
 *          0 if the game can go on
 */
int game_over()
{
    // Returns 1 if game is over, otherwise return 0
    int num_moves_black = 0;
    int num_moves_white = 0;
    int *moves = malloc(sizeof(int) * MAX_MOVES);

    // Check if BLACK has any legal moves
    legal_moves(moves, &num_moves_black, BLACK);

    // Check if WHITE has any legal moves
    legal_moves(moves, &num_moves_white, WHITE);

    free(moves);

    // Game is over if neither player has legal moves
    return (num_moves_black == 0 && num_moves_white == 0);
}

/**
 * Evaluates the board position after a move is made.
 * Currently implements a simple counting heuristic.
 *
 * @param my_colour Colour of the current player
 * @return A score value representing how "good" the position is
 */
int evaluate(int my_colour)
{
    int score = 0;
    int opponent_colour = get_opponent(my_colour);
    int piece_count_me = 0;
    int piece_count_opp = 0;
    int position_score = 0;
    int my_frontier_count = 0;
    int opp_frontier_count = 0;
    int my_stable_count = 0;
    int opp_stable_count = 0;
    int corners_me = 0;
    int corners_opp = 0;

    DEBUG_PRINT(master_fp, "Evaluating board position for colour %d", my_colour);

    // Count pieces and calculate position score
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        for (int j = 0; j < BOARD_SIZE; j++)
        {
            int pos = i * BOARD_SIZE + j; // Calculates the 1d array from 2d coordinates
            if (board[pos] == my_colour)
            {
                piece_count_me++;
                position_score += position_weights[i][j]; // Adds strategic value to position score. Original 2d coords to look up the position value
                // Calculate frontier score
                if (is_frontier(i, j))
                {
                    my_frontier_count++;
                }
            }
            else if (board[pos] == opponent_colour)
            {
                piece_count_opp++;                        // incr piece counter
                position_score -= position_weights[i][j]; // Subtract the position's strategic value from the total position score
                // Calculate opponents frontier score
                if (is_frontier(i, j))
                {
                    opp_frontier_count++;
                }
            }
        }
    }

    // Corners
    int corner_pos[] = {0, 7, 56, 63};
    for (int k = 0; k < 4; k++)
    {
        int val = board[corner_pos[k]];
        if (val == my_colour)
        {
            corners_me++;
        }
        else if (val == opponent_colour)
        {
            corners_opp++;
        }
    }
    int corner_diff = 25 * (corners_me - corners_opp);

    // Calculate stability (count stable disks)
    my_stable_count = count_stable_disks(my_colour);
    opp_stable_count = count_stable_disks(opponent_colour);
    double stability_temp = (my_stable_count - opp_stable_count) / (double)(my_stable_count + opp_stable_count);
    int stability_diff = (int)(100.0 * stability_temp);

    // Calculate mobility (number of legal moves) for both players
    int *moves = malloc(sizeof(int) * MAX_MOVES);
    int my_mobility, opp_mobility;

    // Get my legal moves
    legal_moves(moves, &my_mobility, my_colour);

    // Get opponents legal moves
    legal_moves(moves, &opp_mobility, opponent_colour);

    // Calculate mobility difference
    double mobility_temp = (my_mobility - opp_mobility) / (double)(my_mobility + opp_mobility);
    int mobility_diff = (int)(100.0 * mobility_temp); // Casting to int
    // Free allocated memory
    free(moves);

    // Calculate total pieces and the difference between player and opp pieces
    int total_pieces = piece_count_me + piece_count_opp;
    double piece_temp = (piece_count_me - piece_count_opp) / (double)(piece_count_me + piece_count_opp);
    int piece_diff = (int)(100.0 * piece_temp);

    // Calculate frontier difference (the less the better)
    double frontier_temp = (opp_frontier_count - my_frontier_count) / (double)(opp_frontier_count + my_frontier_count); // Fewer frontier disks is better
    int frontier_diff = (int)(100.0 * frontier_temp);

    // Define game phases based on total pieces on the board
    int phase;
    if (total_pieces <= 20) // 0-20 pieces
    {
        phase = 1; // Starting phase
    }
    else if (total_pieces <= 37) // 21-37 pieces
    {
        phase = 2; // Middle phase
    }
    else if (total_pieces <= 55) // 38-55 pieces
    {
        phase = 3; // End middle phase
    }
    else // 56-64 pieces
    {
        phase = 4; // End phase
    }

    // Weight different evaluation components based on game phase
    switch (phase)
    {
    case 1: // Start phase - Position more important than piece count
        score = (position_score * 5) + (mobility_diff * 10) + (frontier_diff * 3) + (piece_diff * 2) + corner_diff;
        break;

    case 2: // Middle phase - balance between position and piece count
        score = (position_score * 4) + (mobility_diff * 8) + (piece_diff * 1) + (frontier_diff * 3) + (stability_diff * 2) + corner_diff;
        break;

    case 3: // End middle phase - piece count becomes more important
        score = (position_score * 3) + (mobility_diff * 5) + (piece_diff * 3) + (frontier_diff * 2) + (stability_diff * 7) + corner_diff;
        break;

    case 4: // End phase
        if (total_pieces >= 60)
        {
            score = piece_diff * 100; // Heavy weight on winning, near the end, only the final count matter
        }
        else
        {
            // Approaching endgame.
            score = (position_score * 1) + (mobility_diff * 2) + (piece_diff * 10) + (frontier_diff * 1) + (stability_diff * 8) + corner_diff;
        }
        break;
    }

    // Check for game over
    if (game_over())
    {
        // if game is over, evaluate win/loss directly
        if (piece_count_me > piece_count_opp)
        {
            return (piece_count_me - piece_count_opp) * 1000;
        }
        else if (piece_count_me < piece_count_opp)
        {
            return (piece_count_opp - piece_count_me) * -1000;
        }
        else
        {
            return 1000; // Tie
        }
    }
    DEBUG_PRINT(master_fp, "Final evaluation score %d at phase %d", score, phase);
    return score;
}

/**
 * Determines if a disk is on the frontier (has at least one empty adjacent square).
 * Frontier disks are more vulnerable to being flipped.
 *
 * @param row The row position of the disk
 * @param col The column position of the disk
 * @return 1 if the disk is a frontier disk, 0 otherwise
 */

int is_frontier(int row, int col)
{
    // Check all 8 adjacent squares
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            int new_row = row + x;
            int new_col = col + y;

            // If adjacent square is valid and empty, this is a frontier disk
            if (new_row >= 0 && new_row < BOARD_SIZE &&
                new_col >= 0 && new_col < BOARD_SIZE &&
                board[new_row * BOARD_SIZE + new_col] == EMPTY)
            {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * Checks if a disk is stable (cannot be flipped for the remainder of the game).
 * Disk is stable when:
 * - its a corner
 * - its on an edge and cannot be flipped from the sides
 * - its surrounded in all directions by stable disks of the same colour
 *
 * @param row The row position of the disk
 * @param col The column position of the disk
 * @param colour The color of the disk to check
 * @return 1 if the disk is stable, 0 otherwise
 */
int is_stable(int row, int col, int colour)
{
    int pos = row * BOARD_SIZE + col; // Calculates the 1d array from 2d coordinates

    // If pos doesnt have a disk of my colour, not stable
    if (board[pos] != colour)
    {
        return 0;
    }

    // Check if it's a corner (corners are always stable)
    if ((row == 0 || row == BOARD_SIZE - 1) && (col == 0 || col == BOARD_SIZE - 1))
    {
        return 1;
    }

    // Check if it's on an edge and all disks in that direction are filled
    // Horizontal edge stability
    if (row == 0 || row == BOARD_SIZE - 1)
    {
        int stable = 1;
        for (int c = 0; c < BOARD_SIZE; c++)
        {
            if (board[row * BOARD_SIZE + c] == EMPTY)
            {
                stable = 0;
                break;
            }
        }
        if (stable)
            return 1;
    }

    // Vertical edge stability
    if (col == 0 || col == BOARD_SIZE - 1)
    {
        int stable = 1;
        for (int r = 0; r < BOARD_SIZE; r++)
        {
            if (board[r * BOARD_SIZE + col] == EMPTY)
            {
                stable = 0;
                break;
            }
        }
        if (stable)
            return 1;
    }

    // Horizontal, vertical and diagonal vectors
    int directions[8][2] = {
        {-1, 0},  // Up
        {1, 0},   // Down
        {0, -1},  // Left
        {0, 1},   // Right
        {-1, -1}, // Up-Left
        {-1, 1},  // Up-Right
        {1, -1},  // Down-Left
        {1, 1}    // Down-Right
    };

    /// A disk is stable if in each direction it either reaches the edge or another stable disk
    for (int i = 0; i < 8; i++)
    {
        int x = directions[i][0];
        int y = directions[i][1];
        int r = row + x;
        int c = col + y;
        int stable_dir = 0;

        // Move in this direction until we hit an edge or empty space
        while (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE)
        {
            int p = r * BOARD_SIZE + c;
            if (board[p] == EMPTY)
            {
                break; // Not stable in this direction
            }
            if (r == 0 || r == BOARD_SIZE - 1 || c == 0 || c == BOARD_SIZE - 1)
            {
                stable_dir = 1; // Reached an edge, meaning its stable
                break;
            }
            r += x;
            c += y;
        }

        if (!stable_dir)
        {
            return 0; // Not stable in all directions
        }
    }

    return 1; // Stable in all directions
}

/**
 * Counts the total number of stable disks of a given color on the board.
 * Uses an iterative approach to identify stable disks, starting from corners.
 *
 * @param color The color of disks to count
 * @return The number of stable disks of the given color
 */
int count_stable_disks(int color)
{
    int stable_count = 0;
    int *is_stable_disk = malloc(sizeof(int) * BOARD_CAP);

    // Initialize all to unstable
    memset(is_stable_disk, 0, sizeof(int) * BOARD_CAP);

    // First pass, mark corners as stable, corneers cannot be flipped
    int corners[4][2] = {{0, 0}, {0, BOARD_SIZE - 1}, {BOARD_SIZE - 1, 0}, {BOARD_SIZE - 1, BOARD_SIZE - 1}};

    for (int i = 0; i < 4; i++)
    {
        int row = corners[i][0];
        int col = corners[i][1];
        int pos = row * BOARD_SIZE + col;

        if (board[pos] == color)
        {
            is_stable_disk[pos] = 1;
            stable_count++;
        }
    }

    // Keep finding new stable disks until no more can be found
    // Use a fixed-point iteration approach, continue until converge
    int found_new_stable;
    do
    {
        found_new_stable = 0;

        for (int row = 0; row < BOARD_SIZE; row++)
        {
            for (int col = 0; col < BOARD_SIZE; col++)
            {
                int pos = row * BOARD_SIZE + col;

                // Skip already stable disks or non-matching color
                if (is_stable_disk[pos] || board[pos] != color)
                {
                    continue;
                }

                // Check if this disk is stable
                if (is_stable(row, col, color))
                {
                    is_stable_disk[pos] = 1;
                    stable_count++;
                    found_new_stable = 1;
                }
            }
        }
    } while (found_new_stable);

    free(is_stable_disk);
    return stable_count;
}

/**
 * Creates a copy of the current board state.
 * Used before making moves in minimax evaluation.
 *
 * @param copy Pointer to an array where the board copy will be stored
 */
void copy_board(int *copy)
{
    // Copy each cell from the global board to the local copy
    for (int i = 0; i < BOARD_CAP; i++)
    {
        copy[i] = board[i];
    }
}

/**
 * Restores the board state from a previously saved copy.
 * Used after evaluating moves in minimax.
 *
 * @param copy Pointer to an array containing the saved board state
 */
void restore_board(int *copy)
{
    // Copy each cell from the saved copy back to the global board
    for (int i = 0; i < BOARD_CAP; i++)
    {
        board[i] = copy[i];
    }
}

/**
 * Resets the board to the initial state.
 *
 * @param fp pointer to the log file
 */
void reset_board(FILE *fp)
{

    int mid = BOARD_SIZE / 2;
    memset(board, EMPTY, sizeof(int) * BOARD_SIZE * BOARD_SIZE);

    // Set up the initial four pieces in the middle
    board[mid * BOARD_SIZE + mid] = WHITE;
    board[(mid - 1) * BOARD_SIZE + (mid - 1)] = WHITE;
    board[mid * BOARD_SIZE + (mid - 1)] = BLACK;
    board[(mid - 1) * BOARD_SIZE + mid] = BLACK;

    fprintf(fp, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    fprintf(fp, "~~~~~~~~~~~~~ NEW MATCH ~~~~~~~~~~~~\n");
    fprintf(fp, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

    fprintf(fp, "New board state:\n");
}

/**
 * Runs a random strategy. Chooses a random legal move and applies it to the
 * board, then returns the move in the form of an integer (0-361).
 *
 * @param my_colour colour of the player
 * @param fp pointer to the log file
 */
int random_strategy(int my_colour, FILE *fp)
{
    int number_of_moves;
    int *moves = malloc(sizeof(int) * MAX_MOVES);

    /* get all legal moves */
    legal_moves(moves, &number_of_moves, my_colour);

    /* check for pass */
    if (number_of_moves <= 0 || moves[0] == -1)
    {
        fprintf(fp, "\nNo legal moves, passing.\n");
        free(moves);
        return -1;
    }

    /* choose a random move */
    srand((unsigned int)time(NULL));
    int random_index = rand() % number_of_moves;
    int move = moves[random_index];

    free(moves);
    return move;
}

/**
 * Flips pieces in a specific direction after placing a new piece.
 *
 * @param x - row of the newly placed piece
 * @param y - column of the newly placed piece
 * @param dx - row direction
 * @param dy - column direction
 * @param my_colour Colour of the current player
 */
void flip_direction(int x, int y, int dx, int dy, int my_colour)
{
    int i = x + dx;
    int j = y + dy;

    // Move along the direction and flip pieces until we hit a piece of my_colour
    while (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE &&
           board[i * BOARD_SIZE + j] != my_colour)
    {
        board[i * BOARD_SIZE + j] = my_colour;
        i += dx;
        j += dy;
    }
}

/**
 * Applies the given move to the board.
 *
 * @param move move to apply
 * @param my_colour colour of the player
 */
void make_move(int move, int colour)
{
    DEBUG_PRINT(master_fp, "Making move %d for colour %d (row %d, col %d)", move, colour, move / BOARD_SIZE, move % BOARD_SIZE);
    int row = move / BOARD_SIZE;
    int col = move % BOARD_SIZE;
    int opp_colour = (colour == WHITE) ? BLACK : WHITE;
    board[row * BOARD_SIZE + col] = colour;

    // Check and flip in all 8 directions
    for (int dx = -1; dx <= 1; dx++)
    {
        for (int dy = -1; dy <= 1; dy++)
        {
            if (dx == 0 && dy == 0)
                continue; // Skip the current cell

            int i = row + dx;
            int j = col + dy;
            int found_opp = 0;

            // Move in the direction and check for opponent's pieces followed by
            // my piece
            while (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE)
            {
                if (board[i * BOARD_SIZE + j] == opp_colour)
                {
                    found_opp = 1;
                    i += dx;
                    j += dy;
                }
                else if (board[i * BOARD_SIZE + j] == colour && found_opp)
                {
                    flip_direction(row, col, dx, dy, colour);
                    break; // Stop checking this direction as we've found a
                           // valid line
                }
                else
                {
                    break; // No valid line in this direction
                }
            }
        }
    }
}

/**
 * Checks if the given direction is valid. A direction is valid if it sandwiches
 * at least one of the opponent's pieces between the piece being placed and
 * another piece of the player's colour.
 *
 * @param x x-coordinate of the piece being placed
 * @param y y-coordinate of the piece being placed
 * @param dx x-direction to check
 * @param dy y-direction to check
 * @param my_colour colour of the player
 * @param opp_colour colour of the opponent
 * @return 1 if the direction is valid, 0 otherwise
 */
int check_direction(int x, int y, int dx, int dy, int my_colour,
                    int opp_colour)
{
    int i = x + dx;
    int j = y + dy;
    int found_opp = 0; // Flag to check if at least one opponent piece is found

    while (i >= 0 && i < BOARD_SIZE && j >= 0 && j < BOARD_SIZE)
    {
        if (board[i * BOARD_SIZE + j] == opp_colour)
        {
            found_opp = 1;
            i += dx;
            j += dy;
        }
        else if (board[i * BOARD_SIZE + j] == my_colour && found_opp)
        {
            return 1; // Valid direction as it sandwiches opponent's pieces
        }
        else
        {
            return 0; // Either empty or own piece without sandwiching
                      // opponent's pieces
        }
    }
    return 0;
}

/**
 * Gets a list of legal moves for the current board, and stores them in the
 * moves array followed by a -1. Also stores the number of legal moves in the
 * number_of_moves variable.
 *
 * What is a legal move? A legal move is a move that results in at least one of
 * the opponent's pieces being flipped. That is if there is at least one piece
 * of the opponent's colour between the piece being placed and another piece of
 * the player's colour.
 *
 * @param moves array to store the legal moves in
 * @param number_of_moves variable to store the number of legal moves in
 */
void legal_moves(int *moves, int *number_of_moves, int my_colour)
{
    DEBUG_PRINT(master_fp, "Finding legal moves for colour %d", my_colour);
    int opp_colour = (my_colour == WHITE) ? BLACK : WHITE;
    *number_of_moves = 0;

    for (int i = 0; i < BOARD_SIZE; i++)
    {
        for (int j = 0; j < BOARD_SIZE; j++)
        {
            if (board[i * BOARD_SIZE + j] != EMPTY)
                continue;

            int moveFound =
                0; // Flag to indicate if a legal move is found for the cell

            // Check all 8 directions from the current cell
            for (int dx = -1; dx <= 1 && !moveFound; dx++)
            {
                for (int dy = -1; dy <= 1 && !moveFound; dy++)
                {
                    if (dx == 0 && dy == 0)
                        continue; // Skip checking the current cell

                    if (check_direction(i, j, dx, dy, my_colour, opp_colour))
                    {
                        moves[(*number_of_moves)++] = i * BOARD_SIZE + j;
                        moveFound = 1; // A legal move is found, no need to
                                       // check other directions
                    }
                }
            }
        }
    }
    DEBUG_PRINT(master_fp, "Found %d legal moves for colour %d", *number_of_moves, my_colour);
    moves[*number_of_moves] = -1; // End of moves
}

/**
 * Initialises the board for the game.
 */
void initialise_board(void)
{
    int mid = BOARD_SIZE / 2;

    board = malloc(sizeof(int) * BOARD_SIZE * BOARD_SIZE);
    memset(board, EMPTY, sizeof(int) * BOARD_SIZE * BOARD_SIZE);

    /* plave initial pieces */
    board[mid * BOARD_SIZE + mid] = WHITE;
    board[(mid - 1) * BOARD_SIZE + (mid - 1)] = WHITE;
    board[mid * BOARD_SIZE + (mid - 1)] = BLACK;
    board[(mid - 1) * BOARD_SIZE + mid] = BLACK;
}

/**
 * Prints the board to the given file with improved aesthetics.
 *
 * @param fp pointer to the file to print to
 */
void print_board(FILE *fp)
{
    if (fp == NULL)
    {
        return; // File pointer is not valid
    }

    fprintf(fp, "  ");
    for (int i = 0; i < BOARD_SIZE; ++i)
    {
        fprintf(fp, "%d ", i); // Print column numbers
    }
    fprintf(fp, "\n");

    for (int i = 0; i < BOARD_SIZE; ++i)
    {
        fprintf(fp, "%d ", i); // Print row numbers
        for (int j = 0; j < BOARD_SIZE; ++j)
        {
            if (board[i * BOARD_SIZE + j] == EMPTY)
            {
                fprintf(fp, ". "); // Print a dot for empty spaces
            }
            else if (board[i * BOARD_SIZE + j] == BLACK)
            {
                fprintf(fp, "B "); // Print B for Black pieces
            }
            else if (board[i * BOARD_SIZE + j] == WHITE)
            {
                fprintf(fp, "W "); // Print W for White pieces
            }
        }
        fprintf(fp, "\n");
    }
}

/**
 * Frees the memory allocated for the board.
 */
void free_board(void) { free(board); }

/**
 * Initialises the master process for communication with the IF wrapper and set
 * up the log file.
 * @param argc command line argument count
 * @param argv command line argument vector
 * @param time_limit time limit for the game
 * @param my_colour colour of the player
 * @param fp pointer to the log file
 * @return 1 if initialisation was successful, 0 otherwise
 */
int initialise_master(int argc, char *argv[], int *time_limit, int *my_colour,
                      FILE **fp)
{
    unsigned long int ip = inet_addr(argv[1]);
    int port = atoi(argv[2]);
    *time_limit = atoi(argv[3]);
    *my_colour = atoi(argv[4]);

    setup_board_weights();

    move_stack.head = NULL;
    move_stack.size = 0;

    /* open file for logging */
    *fp = fopen(PLAYER_NAME_LOG, "w");

    if (*fp == NULL)
    {
        printf("Could not open log file\n");
        return 0;
    }

    fprintf(*fp, "Initialising communication.\n");

    /* initialise comms to IF wrapper */
    if (!initialise_comms(ip, port))
    {
        printf("Could not initialise comms\n");
        return 0;
    }

    fprintf(*fp, "Communication initialised \n");

    fprintf(*fp, "Let the game begin...\n");
    fprintf(*fp, "My name: %s\n", PLAYER_NAME_LOG);
    fprintf(*fp, "My colour: %d\n", *my_colour);
    fprintf(*fp, "Board size: %d\n", BOARD_SIZE);
    fprintf(*fp, "Time limit: %d\n", *time_limit);
    fprintf(*fp, "-----------------------------------\n");
    print_board(*fp);

    fflush(*fp);

    return 1;
}

/**
 * Initialises worker process. Each worker process will have
 * its own log file for debugging.
 *
 * @param rank rank of the worker process
 * @param my_colour the colour of this player
 * @param fp pointer to file pointer that will be set to the worker's log
 * @return 1 if initialisation was successful
 */
int initialise_worker(int rank, int my_colour, FILE **fp)
{
    int name_len = strlen(WORKER_NAME_LOG);
    char *file_name = malloc(sizeof(char) * (name_len + 1));
    strcpy(file_name, WORKER_NAME_LOG);
    for (int i = 0; i < name_len; i++)
    {
        if (file_name[i] == 'x')
        {
            file_name[i] = '0' + rank;
            break;
        }
    }

    current_alpha = INT_MIN;

    /* open file for logging */
    *fp = fopen(file_name, "w");
    if (*fp == NULL)
    {
        printf("Could not open log file\n");
    }

    fprintf(*fp, "Initialising worker process %d\n", rank);

    fprintf(*fp, "Let the game begin...\n");
    fprintf(*fp, "Worker process: %d\n", rank);
    fprintf(*fp, "My colour: %d\n", my_colour);
    fprintf(*fp, "Board size: %d\n", BOARD_SIZE);
    fprintf(*fp, "-----------------------------------\n");
    print_board(*fp);

    // fprintf(*fp, "process %d has the log file %s\n", rank, file_name);
    fflush(*fp);

    free(file_name);

    setup_board_weights();
    return 1;
}

/**
 * Initialises the board weights array from the position_weights.
 * Position weights are used in the evaluation function to assign different
 * values to different board positions.
 * It transforms the 2D array into a 1D array for easier access during board
 * evaluation. Corners, edges, and center positions have different strategic values.
 */
void setup_board_weights()
{
    board_weights = malloc(sizeof(int) * BOARD_CAP);
    int w = 0;
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        for (int j = 0; j < BOARD_SIZE; j++)
        {
            board_weights[w++] = position_weights[i][j]; // 2D -> 1D
        }
    }
}

void debug_log(FILE *f, const char *fmt, ...)
{
    if (!f)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    fprintf(f, "[DEBUG]");
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    fflush(f);
    va_end(args);
}
