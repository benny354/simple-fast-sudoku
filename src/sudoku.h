#ifndef SUDOKU_H
#define SUDOKU_H

#include <stdint.h>

typedef struct {
    uint8_t given[81];
    uint8_t solution[81];
    int clues;
} Sudoku;

/* A nonzero seed produces a reproducible stream, with no global RNG state. */
void sudoku_generate(Sudoku *out, int target_clues, uint32_t *rng);
/* Returns 0, 1, or limit (stops searching as soon as limit is reached). */
int sudoku_count_solutions(const uint8_t board[81], int limit);
int sudoku_is_complete(const uint8_t board[81]);
int sudoku_conflict(const uint8_t board[81], int cell);

#endif
