#include "../src/sudoku.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void require(int value, const char *message) {
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main(int argc, char **argv) {
    Sudoku puzzle, same;
    uint8_t invalid[81] = {0};
    uint32_t rng = 0x71538A19u, seed;
    int targets[] = {42, 34, 28};
    int i, j, count = argc > 1 ? atoi(argv[1]) : 1000;
    int dump = argc > 2 && strcmp(argv[2], "--dump") == 0;
    clock_t start = clock();
    double slowest = 0;
    invalid[0] = 1; invalid[1] = 1;
    require(sudoku_count_solutions(invalid, 2) == 0, "duplicate givens rejected");
    invalid[1] = 0; invalid[0] = 10;
    require(sudoku_count_solutions(invalid, 2) == 0, "out of range rejected");
    memset(invalid, 0, 81);
    require(sudoku_count_solutions(invalid, 2) == 2, "multiple solutions bounded");
    require(!sudoku_is_complete(invalid), "empty board incomplete");
    require(sudoku_count_solutions(invalid, 0) == 0, "zero limit");
    for (i = 0; i < count; ++i) {
        int actual = 0, target = targets[i % 3];
        clock_t tick = clock();
        seed = rng;
        sudoku_generate(&puzzle, target, &rng);
        if ((double)(clock() - tick) / CLOCKS_PER_SEC > slowest)
            slowest = (double)(clock() - tick) / CLOCKS_PER_SEC;
        require(sudoku_is_complete(puzzle.solution), "full grid valid");
        require(sudoku_count_solutions(puzzle.given, 2) == 1, "exactly one solution");
        require(!sudoku_is_complete(puzzle.given), "puzzle has empty cells");
        for (j = 0; j < 81; ++j) {
            if (puzzle.given[j]) {
                ++actual;
                require(puzzle.given[j] == puzzle.solution[j], "givens match solution");
            }
        }
        require(actual == puzzle.clues && actual >= target, "clue count consistent");
        if (i < 5) {
            sudoku_generate(&same, target, &seed);
            require(memcmp(puzzle.given, same.given, 81) == 0, "seed reproducibility");
        }
        if (dump) {
            for (j = 0; j < 81; ++j) putchar('0' + puzzle.given[j]);
            putchar(',');
            for (j = 0; j < 81; ++j) putchar('0' + puzzle.solution[j]);
            putchar('\n');
        }
    }
    fprintf(stderr, "PASS: %d unique puzzles; %.3f sec total; slowest generation %.3f sec\n",
            count, (double)(clock() - start) / CLOCKS_PER_SEC, slowest);
    return 0;
}
