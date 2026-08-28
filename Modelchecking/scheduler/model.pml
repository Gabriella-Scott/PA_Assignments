/* ---- Configuration ---- */
#define NUM_PROCS     3
#define NUM_WORKERS   2
#define NUM_RESOURCES 2
#define MAX_INSTR     3

/* ---- Process states ---- */
#define READY      0
#define RUNNING    1
#define WAITING    2
#define TERMINATED 3

/* ---- Shared state ---- */
byte pcb_state[NUM_PROCS + 1];
byte instr_count[NUM_PROCS + 1];
byte waiting_for[NUM_PROCS + 1];
byte resource_owner[NUM_RESOURCES + 1];

chan ready_q   = [NUM_PROCS] of { byte };
chan waiting_q = [NUM_PROCS] of { byte, byte };