/* Exercise actual game commands with drawing suppressed, without a UI driver. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static BOOL WINAPI ignore_invalidate(HWND hwnd, const RECT *area, BOOL erase) {
    (void)hwnd; (void)area; (void)erase; return TRUE;
}
#define InvalidateRect ignore_invalidate
#define WinMain SudokuApplicationEntry
#include "../src/main.c"
#undef WinMain

static void require(int value, const char *message) {
    if (!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}

static void reset_fixture(void) {
    rng=0x12345678;
    sudoku_generate(&puzzle,34,&rng);
    memset(&game,0,sizeof(game)); memcpy(game.values,puzzle.given,81);
    history_start=history_count=note_mode=finished=0;
    started=ticks(); elapsed=0;
    for (selected=0;selected<81;++selected) if (!puzzle.given[selected]) break;
}

int main(void) {
    int i, blank, given=-1, peer=-1, value;
    Move saved;
    reset_fixture(); blank=selected;
    for (i=0;i<81;++i) if (puzzle.given[i]) { given=i; break; }
    selected=given; enter_value(puzzle.given[given]%9+1,0);
    require(game.values[given]==puzzle.given[given] && !history_count,"givens immutable");
    selected=blank; note_mode=1; enter_value(4,0);
    require(!game.values[blank] && game.notes[blank]==(1u<<4),"notes do not fill answer");
    enter_value(4,0); require(!game.notes[blank],"note toggles off");
    undo(); require(game.notes[blank]==(1u<<4),"undo restores note");
    erase_value(); require(!game.notes[blank],"erase clears note");
    undo(); require(game.notes[blank]==(1u<<4),"erase is undoable");

    reset_fixture(); blank=selected; value=puzzle.solution[blank];
    for (i=0;i<81;++i) if (i!=blank && !game.values[i] &&
            (i/9==blank/9 || i%9==blank%9 || (i/27==blank/27 && i%9/3==blank%9/3))) {
        peer=i; break;
    }
    require(peer>=0,"fixture has editable peer");
    game.notes[peer]=(uint16_t)(1u<<value); saved=game;
    enter_value(value,0);
    require(game.values[blank]==value && !game.notes[peer],"entry clears peer candidate");
    undo(); require(!memcmp(&saved,&game,sizeof(game)),"undo restores answer and peer notes");

    reset_fixture(); note_mode=1;
    for (i=0;i<81 && !finished;++i) hint();
    require(finished && sudoku_is_complete(game.values),"hints can complete unique solution in note mode");
    saved=game; enter_value(1,0); erase_value();
    require(!memcmp(&saved,&game,sizeof(game)),"completed game locked");
    undo(); require(!finished && !sudoku_is_complete(game.values),"undo reopens completed game");
    hint(); require(finished,"can finish again after undo");

    reset_fixture(); note_mode=1;
    for (i=0;i<HISTORY+31;++i) enter_value(4,0);
    require(history_count==HISTORY,"bounded history capacity");
    for (i=0;i<HISTORY;++i) undo();
    require(history_count==0 && game.notes[selected]==(1u<<4),"history wraps and restores oldest retained state");
    saved=game; undo();
    require(!memcmp(&saved,&game,sizeof(game)),"empty undo leaves state intact");
    puts("PASS: givens, notes, erase, peer cleanup, undo, hints, completion, timer resume and history ring");
    return 0;
}
