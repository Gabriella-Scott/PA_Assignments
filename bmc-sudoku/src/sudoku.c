// Sudoku model for CBMC, one-hot bitmask encoding
// VERIFICATION FAILED -> solution in trace
// VERIFICATION SUCCESSFUL -> no grid passes rules -> unsolvable
// puzzle passed in by wrapper: -DPUZZLE="{5,3,....}"

#include <assert.h>

// default puzzle
#ifndef PUZZLE
#define PUZZLE {               \
    5, 3, 0, 0, 7, 0, 0, 0, 0, \
    6, 0, 0, 1, 9, 5, 0, 0, 0, \
    0, 9, 8, 0, 0, 0, 0, 6, 0, \
    8, 0, 0, 0, 6, 0, 0, 0, 3, \
    4, 0, 0, 8, 0, 3, 0, 0, 1, \
    7, 0, 0, 0, 2, 0, 0, 0, 6, \
    0, 6, 0, 0, 0, 0, 2, 8, 0, \
    0, 0, 0, 4, 1, 9, 0, 0, 5, \
    0, 0, 0, 0, 8, 0, 0, 7, 9}
#endif // PUZZLE

#define ALL 0x1FFu // bits 0-8 = digits 1-9

// Part 2: wrapper passes earlier solutions in to rule out
// BLOCKED = flat NBLOCK x 81 mask array. default: none blocked -> prt 1 uses same model unchanged
#ifndef NBLOCK
#define NBLOCK 0
#define BLOCKED {0}
#endif // BLOCKED

int main()
{
    int puzzle[81] = PUZZLE; // givens, flat
    unsigned int g[9][9];  // local + uninitialised -> nondet
    // global would be zeroed

    int r, c, k, b; // row, col, cell in group, box
    // cell range: 1-9
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            unsigned int m = g[r][c]; // current cell's bitmask
            // exactly 1 bit set and in range
            // m & (m-1) clears lowest set bit -> 0 only if 1 bit
            __CPROVER_assume(m != 0 && m <= 256 && (m & (m - 1)) == 0);

            // given: fix to that digit's bit
            if (puzzle[r * 9 + c])
            {
                __CPROVER_assume(m == (1u << (puzzle[r * 9 + c] - 1)));
            }
        }
    }

    // rows: 9 one-bit masks OR-ing to all 9 bits -> all distinct
    for (r = 0; r < 9; r++)
    {
        unsigned int o = 0;
        for (c = 0; c < 9; c++) 
        {
            o |= g[r][c]; // accumulate bitmask for this row
        }
        __CPROVER_assume(o == ALL); // all 9 bits set
    }

    // columns
    for (c = 0; c < 9; c++)
    {
        unsigned int o = 0;
        for (r = 0; r < 9; r++)
        {
            o |= g[r][c]; // accumulate bitmask for this column
        }
        __CPROVER_assume(o == ALL); // all 9 bits set
    }

    // boxes -> box b: tl (3*(b/3), 3*(b%3)); cell k in box: (k/3, k%3)
    for (b = 0; b < 9; b++)
    {
        unsigned int o = 0;
        // accumulate bitmask for this box
        for (k = 0; k < 9; k++)
        {
            o |= g[3 * (b / 3) + k / 3][3 * (b % 3) + k % 3];
        }
        __CPROVER_assume(o == ALL);
    }

#if NBLOCK > 0 // rule out earlier solns: each must differ in >= 1 cell
    unsigned int blocked[NBLOCK * 81] = BLOCKED;
    for (int i = 0; i < NBLOCK; i++)
    {
        unsigned int diff = 0;
        for (int j = 0; j < 81; j++)
        {
            diff |= g[j / 9][j % 9] ^ blocked[i * 81 + j];
        }
        __CPROVER_assume(diff != 0); // must differ in at least one cell
    }
#endif // NBLOCK > 0

    // target ->reachable only if all rules holds
    assert(0);
    return 0;
}