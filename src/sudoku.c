#include "sudoku.h"
#include <string.h>

#define ALL 0x3FEu

typedef struct {
    uint8_t cell[81];
    unsigned row[9], col[9], box[9];
} Search;

static uint32_t random32(uint32_t *state) {
    uint32_t x = *state ? *state : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

static unsigned uniform(uint32_t *state, unsigned bound) {
    uint32_t x, threshold = (0u - bound) % bound;
    do { x = random32(state); } while (x < threshold);
    return x % bound;
}

static void shuffle(int *items, int count, uint32_t *state) {
    int i;
    for (i = count - 1; i > 0; --i) {
        int j = (int)uniform(state, (unsigned)i + 1);
        int t = items[i]; items[i] = items[j]; items[j] = t;
    }
}

static int bits(unsigned value) {
    int n = 0;
    while (value) { value &= value - 1; ++n; }
    return n;
}

static int init_search(Search *s, const uint8_t board[81]) {
    int i;
    memset(s, 0, sizeof(*s));
    memcpy(s->cell, board, 81);
    for (i = 0; i < 81; ++i) {
        int v = board[i], r = i / 9, c = i % 9, b = r / 3 * 3 + c / 3;
        unsigned mask;
        if (!v) continue;
        if (v > 9) return 0;
        mask = 1u << v;
        if ((s->row[r] | s->col[c] | s->box[b]) & mask) return 0;
        s->row[r] |= mask; s->col[c] |= mask; s->box[b] |= mask;
    }
    return 1;
}

/* MRV: branch on the cell with the fewest legal values. A randomized search
   makes full grids; a deterministic bounded search proves uniqueness. */
static int solve(Search *s, int limit, uint32_t *rng) {
    int i, best = -1, smallest = 10, count = 0;
    unsigned options = 0;
    int values[9], n = 0, r, c, b;
    for (i = 0; i < 81; ++i) {
        unsigned choices;
        int size;
        if (s->cell[i]) continue;
        r = i / 9; c = i % 9; b = r / 3 * 3 + c / 3;
        choices = ALL & ~(s->row[r] | s->col[c] | s->box[b]);
        size = bits(choices);
        if (!size) return 0;
        if (size < smallest) {
            smallest = size; best = i; options = choices;
            if (size == 1) break;
        }
    }
    if (best < 0) return 1;
    r = best / 9; c = best % 9; b = r / 3 * 3 + c / 3;
    for (i = 1; i <= 9; ++i) if (options & (1u << i)) values[n++] = i;
    if (rng) shuffle(values, n, rng);
    for (i = 0; i < n; ++i) {
        int found;
        unsigned mask = 1u << values[i];
        s->cell[best] = (uint8_t)values[i];
        s->row[r] |= mask; s->col[c] |= mask; s->box[b] |= mask;
        found = solve(s, limit - count, rng);
        if (rng && found) return 1; /* Keep the completed randomized grid. */
        count += found;
        s->cell[best] = 0;
        s->row[r] &= ~mask; s->col[c] &= ~mask; s->box[b] &= ~mask;
        if (count >= limit) break;
    }
    return count;
}

int sudoku_count_solutions(const uint8_t board[81], int limit) {
    Search s;
    if (limit < 1 || !init_search(&s, board)) return 0;
    return solve(&s, limit, 0);
}

void sudoku_generate(Sudoku *out, int target_clues, uint32_t *rng) {
    Search s;
    uint8_t empty[81] = {0};
    int order[81], i;
    if (target_clues < 24) target_clues = 24;
    if (target_clues > 81) target_clues = 81;
    init_search(&s, empty);
    solve(&s, 1, rng);
    memcpy(out->solution, s.cell, 81);
    memcpy(out->given, s.cell, 81);
    out->clues = 81;
    for (i = 0; i < 81; ++i) order[i] = i;
    shuffle(order, 81, rng);
    for (i = 0; i < 81 && out->clues > target_clues; ++i) {
        int cell = order[i];
        uint8_t value = out->given[cell];
        out->given[cell] = 0;
        if (sudoku_count_solutions(out->given, 2) == 1) --out->clues;
        else out->given[cell] = value;
    }
}

int sudoku_conflict(const uint8_t board[81], int cell) {
    int i, r, c, br, bc, value;
    if (cell < 0 || cell >= 81) return 0;
    value = board[cell];
    if (!value) return 0;
    if (value > 9) return 1;
    r = cell / 9; c = cell % 9; br = r / 3 * 3; bc = c / 3 * 3;
    for (i = 0; i < 9; ++i) {
        int a = r * 9 + i, b = i * 9 + c;
        int d = (br + i / 3) * 9 + bc + i % 3;
        if ((a != cell && board[a] == value) ||
            (b != cell && board[b] == value) ||
            (d != cell && board[d] == value)) return 1;
    }
    return 0;
}

int sudoku_is_complete(const uint8_t board[81]) {
    int i;
    for (i = 0; i < 81; ++i) if (!board[i] || sudoku_conflict(board, i)) return 0;
    return 1;
}
