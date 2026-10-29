"""Independently verify puzzles using Python sets, separate from the C solver.

Usage: python tests/verify_independent.py [number_of_puzzles]
Run build.ps1 -Test first. Python is only a development/test dependency.
"""
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def solutions(board, limit=2):
    board = list(board)
    rows = [set(range(1, 10)) for _ in range(9)]
    cols = [set(range(1, 10)) for _ in range(9)]
    boxes = [set(range(1, 10)) for _ in range(9)]
    for i, value in enumerate(board):
        if value:
            r, c = divmod(i, 9)
            b = r // 3 * 3 + c // 3
            if value not in rows[r] & cols[c] & boxes[b]:
                return []
            rows[r].remove(value)
            cols[c].remove(value)
            boxes[b].remove(value)
    result = []

    def search():
        best, options = -1, set(range(1, 11))
        for i, value in enumerate(board):
            if value:
                continue
            r, c = divmod(i, 9)
            candidates = rows[r] & cols[c] & boxes[r // 3 * 3 + c // 3]
            if not candidates:
                return
            if len(candidates) < len(options):
                best, options = i, candidates
                if len(options) == 1:
                    break
        if best == -1:
            result.append(board.copy())
            return
        r, c = divmod(best, 9)
        b = r // 3 * 3 + c // 3
        for value in sorted(options):
            board[best] = value
            rows[r].remove(value)
            cols[c].remove(value)
            boxes[b].remove(value)
            search()
            board[best] = 0
            rows[r].add(value)
            cols[c].add(value)
            boxes[b].add(value)
            if len(result) >= limit:
                return

    search()
    return result


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    started = time.perf_counter()
    run = subprocess.run([str(ROOT / 'build' / 'core_test.exe'), str(count), '--dump'],
                         capture_output=True, text=True, check=True)
    puzzles = run.stdout.splitlines()
    assert len(puzzles) == count
    seen = set()
    for line in puzzles:
        given, answer = line.split(',')
        assert given not in seen, 'Repeated puzzle in test stream'
        seen.add(given)
        found = solutions(map(int, given))
        assert len(found) == 1, given
        assert found[0] == list(map(int, answer)), given
    print(run.stderr.strip())
    print(f'PASS: independent Python solver verified {count} unique puzzles '
          f'in {time.perf_counter() - started:.3f}s')


if __name__ == '__main__':
    main()
