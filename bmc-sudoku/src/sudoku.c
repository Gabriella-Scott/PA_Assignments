// baseline Sudoku model for CBMC
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

int main()
{
    int puzzle[81] = PUZZLE; // givens, flat
    int g[9][9];             // local + uninitialised -> nondet
    // global would be zeroed

    int r, c, k, l; // row, column, block row, block column
    // cell range: 1-9
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            __CPROVER_assume(g[r][c] >= 1 && g[r][c] <= 9);
        }
    }

    // givens: fix non-empty cells
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            if (puzzle[r * 9 + c] != 0)
            {
                __CPROVER_assume(g[r][c] == puzzle[r * 9 + c]);
            }
        }
    }

    // rows: all distinct. 9 cells, vals 1-9, distinct -> each digit once
    for (r = 0; r < 9; r++)
    {
        for (k = 0; k < 9; k++)
        {
            for (l = k + 1; l < 9; l++)
            {
                __CPROVER_assume(g[r][k] != g[r][l]);
            }
        }
    }

    // columns: all distinct
    for (c = 0; c < 9; c++)
    {
        for (k = 0; k < 9; k++)
        {
            for (l = k + 1; l < 9; l++)
            {
                __CPROVER_assume(g[k][c] != g[l][c]);
            }
        }
    }

    // boxes: all distinct -> box b: top-left (3*(b/3), 3*(b%3)); cell k in box: (k/3, k%3)
    int b;
    for (b = 0; b < 9; b++)
    {
        for (k = 0; k < 9; k++)
        {
            for (l = k + 1; l < 9; l++)
            {
                __CPROVER_assume(
                    g[3 * (b / 3) + k / 3][3 * (b % 3) + k % 3] !=
                    g[3 * (b / 3) + l / 3][3 * (b % 3) + l % 3]);
            }
        }
    }
    // target: reachable only if all rules holds
    assert(0);
    return 0;
}