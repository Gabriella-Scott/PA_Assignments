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

int main()
{
    int puzzle[81] = PUZZLE; // givens, flat
    unsigned int g[9][9]; // local + uninitialised -> nondet
    // global would be zeroed

    int r, c, k, b; // row, col, cell in group, box
    // cell range: 1-9
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            unsigned int m = g[r][c];
            // exactly 1 bit set and in range
            // m & (m-1) clears lowest set bit -> 0 only if 1 bit
            __CPROVER_assume(m != 0 && m <= 256 && (m & (m - 1)) == 0);

            //given: fix to that digit's bit
            if (puzzle[r * 9 + c] )
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
                o |= g[r][c];            
        }
        __CPROVER_assume(o == ALL); // all 9 bits set
    }

    // columns
    for (c = 0; c < 9; c++)
    {
        unsigned int o = 0;
        for (r = 0; r < 9; r++)
        {
            o |= g[r][c];
        }
        __CPROVER_assume(o == ALL); // all 9 bits set
    }

    // boxes -> box b: tl (3*(b/3), 3*(b%3)); cell k in box: (k/3, k%3) 
    for (b = 0; b < 9; b++)
    {
        unsigned int o = 0;
        for (k = 0; k < 9; k++)
        {
           o |= g[3 * (b / 3) + k / 3][3 * (b % 3) + k % 3];
        }
        __CPROVER_assume(o == ALL);
    }

    // target: reachable only if all rules holds
    assert(0);
    return 0;
}