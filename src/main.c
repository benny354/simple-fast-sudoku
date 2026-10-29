#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "sudoku.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

#define APP_W 980
#define APP_H 800
#define HISTORY 256
#define BG RGB(247,248,250)
#define INK RGB(33,44,62)
#define MUTED RGB(116,128,145)
#define BLUE RGB(49,100,220)
#define PALE RGB(231,239,255)
#define LINE RGB(219,225,234)
#define WHITE RGB(255,255,255)
#define GREEN RGB(34,133,107)
#define RED RGB(201,67,77)

typedef struct {
    uint8_t values[81];
    uint16_t notes[81];
} Move;

static Sudoku puzzle;
static Move game, history[HISTORY];
static int history_start, history_count, selected = -1, difficulty = 1;
static int note_mode, finished, hover, status_kind;
static uint32_t rng;
static ULONGLONG started, elapsed;
static wchar_t status_text[160];
static HWND window;
static HDC buffer_dc;
static HBITMAP buffer_bitmap;
static HGDIOBJ buffer_original;
static int buffer_w, buffer_h;
static double scale = 1.0;
static int offset_x, offset_y;
static HFONT fonts[8];
static const int font_sizes[8] = {11, 13, 15, 18, 26, 30, 28, 12};
static const wchar_t *levels[3] = {L"轻松", L"普通", L"挑战"};
static const int clue_targets[3] = {42, 34, 28};

/* Resolve newer Win32 exports dynamically so old and current C SDKs can build. */
static ULONGLONG ticks(void) {
    typedef ULONGLONG (WINAPI *TickFn)(void);
    static TickFn fn;
    if (!fn) fn=(TickFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTickCount64");
    return fn ? fn() : GetTickCount();
}

static void brush_color(HDC dc, COLORREF color) {
    typedef COLORREF (WINAPI *BrushFn)(HDC,COLORREF);
    static BrushFn fn;
    if (!fn) fn=(BrushFn)GetProcAddress(GetModuleHandleW(L"gdi32.dll"),"SetDCBrushColor");
    if (fn) fn(dc,color);
}

static int sx(int x) { return offset_x + (int)(x * scale + 0.5); }
static int sy(int y) { return offset_y + (int)(y * scale + 0.5); }
static int length(int x) { int n = (int)(x * scale + 0.5); return n > 0 ? n : 1; }
static RECT rect(int x, int y, int w, int h) {
    RECT r; r.left = sx(x); r.top = sy(y); r.right = sx(x+w); r.bottom = sy(y+h); return r;
}
static int inside(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x < bx+bw && y >= by && y < by+bh;
}
static void repaint(void) { InvalidateRect(window, 0, FALSE); }

static void set_status(const wchar_t *message, int kind) {
    lstrcpynW(status_text, message, 160); status_kind = kind; repaint();
}

static void make_fonts(void) {
    int i;
    for (i = 0; i < 8; ++i) {
        if (fonts[i]) DeleteObject(fonts[i]);
        fonts[i] = CreateFontW(-length(font_sizes[i]), 0, 0, 0,
            (i == 3 || i == 5 || i == 6) ? FW_SEMIBOLD : FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
            (i == 4 || i == 6) ? L"Segoe UI" : L"Microsoft YaHei UI");
    }
}

static void fill(HDC dc, int x, int y, int w, int h, COLORREF color) {
    RECT r = rect(x,y,w,h);
    brush_color(dc, color); FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void rounded(HDC dc, int x, int y, int w, int h, int radius,
                    COLORREF color, COLORREF edge) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, length(1), edge);
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, sx(x), sy(y), sx(x+w), sy(y+h), length(radius*2), length(radius*2));
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    DeleteObject(brush); DeleteObject(pen);
}

static void text(HDC dc, const wchar_t *value, int x, int y, int w, int h,
                 int font, COLORREF color, UINT align) {
    RECT r = rect(x,y,w,h);
    HGDIOBJ old = SelectObject(dc, fonts[font]);
    SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,value,-1,&r,DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | align);
    SelectObject(dc,old);
}

static void button(HDC dc, int id, int x, int y, int w, int h,
                   const wchar_t *label, int active, int primary) {
    COLORREF bg = WHITE, fg = INK, border = LINE;
    if (primary) { bg = hover == id ? RGB(35,82,194) : BLUE; fg = WHITE; border = bg; }
    else if (active) { bg = PALE; fg = BLUE; border = RGB(181,202,246); }
    else if (hover == id) { bg = RGB(241,245,252); border = RGB(188,201,224); }
    rounded(dc,x,y,w,h,9,bg,border);
    text(dc,label,x,y,w,h,1,fg,DT_CENTER);
}

static void snapshot(void) {
    int slot;
    if (history_count == HISTORY) {
        history_start = (history_start + 1) % HISTORY; --history_count;
    }
    slot = (history_start + history_count) % HISTORY;
    history[slot] = game; ++history_count;
}

static void update_finished(void) {
    int was_finished = finished;
    finished = sudoku_is_complete(game.values);
    if (finished && !was_finished) {
        elapsed = ticks() - started;
        set_status(L"完成了！每一行、每一列和每一宫，都恰好包含 1—9。", 1);
    } else if (!finished && was_finished) {
        started = ticks() - elapsed;
        set_status(L"已撤销，继续解题吧。", 0);
    }
}

static void new_game(void) {
    int i;
    SetCursor(LoadCursorW(0,IDC_WAIT));
    sudoku_generate(&puzzle,clue_targets[difficulty],&rng);
    memset(&game,0,sizeof(game));
    memcpy(game.values,puzzle.given,81);
    history_start = history_count = 0;
    selected = -1; note_mode = finished = 0; elapsed = 0;
    for (i=0;i<81;++i) if (!game.values[i]) { selected=i; break; }
    started = ticks();
    SetCursor(LoadCursorW(0,IDC_ARROW));
    set_status(L"已为你生成新题，且已验证只有一个解。", 0);
}

static int editable(void) {
    if (finished) { set_status(L"本局已完成。点击「新的一局」继续。",1); return 0; }
    if (selected < 0) { set_status(L"先选择一个空白格。",0); return 0; }
    if (puzzle.given[selected]) {
        set_status(L"深色数字是题目给定的，请选择其他格子。",0); return 0;
    }
    return 1;
}

static void remove_peer_notes(int cell, int value) {
    int i, r = cell/9, c=cell%9, b=r/3*3+c/3;
    for (i=0;i<81;++i)
        if (i/9==r || i%9==c || i/27*3+(i%9)/3==b)
            game.notes[i] &= (uint16_t)~(1u<<value);
}

static void enter_value(int value, int force) {
    if (!editable()) return;
    if (note_mode && !force) {
        if (game.values[selected]) {
            set_status(L"笔记只用于空白格，请先擦除已填数字。",0); return;
        }
        snapshot(); game.notes[selected] ^= (uint16_t)(1u << value);
        set_status(L"笔记模式：再次输入同一数字可取消标记。",0);
    } else {
        if (game.values[selected] == value) return;
        snapshot(); game.values[selected] = (uint8_t)value; game.notes[selected] = 0;
        remove_peer_notes(selected,value);
        if (sudoku_conflict(game.values,selected))
            set_status(L"红色表示同一行、列或九宫格内出现了重复数字。",2);
        else set_status(L"方向键移动 · 1—9 填写 · 空格切换笔记",0);
        update_finished();
    }
    repaint();
}

static void erase_value(void) {
    if (!editable()) return;
    if (!game.values[selected] && !game.notes[selected]) return;
    snapshot(); game.values[selected]=0; game.notes[selected]=0;
    set_status(L"已擦除当前格。Ctrl+Z 可以撤销。",0);
}

static void undo(void) {
    if (!history_count) { set_status(L"暂时没有可撤销的操作。",0); return; }
    --history_count; game = history[(history_start+history_count)%HISTORY];
    set_status(L"已撤销上一步。",0); update_finished(); repaint();
}

static void hint(void) {
    int i;
    if (finished) return;
    if (selected<0 || puzzle.given[selected] || game.values[selected]==puzzle.solution[selected]) {
        for (i=0;i<81;++i)
            if (!puzzle.given[i] && game.values[i]!=puzzle.solution[i]) { selected=i; break; }
    }
    if (selected<0 || puzzle.given[selected]) return;
    enter_value(puzzle.solution[selected],1);
    if (!finished) set_status(L"已填入当前格的正确答案，提示也可以撤销。",0);
}

static int allow_new_game(void) {
    int i, has_progress=0;
    for (i=0;i<81;++i)
        if (game.values[i]!=puzzle.given[i] || game.notes[i]) { has_progress=1; break; }
    if (has_progress && !finished)
        return MessageBoxW(window,L"开始新题会清除本局进度。继续吗？",L"开始新的一局",
            MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;
    return 1;
}

static void action(int id) {
    if (id>=1 && id<=9) enter_value(id,0);
    else if (id>=20 && id<=22) {
        if (id-20==difficulty) return;
        if (allow_new_game()) { difficulty=id-20; new_game(); }
    } else if (id==30) {
        note_mode=!note_mode;
        set_status(note_mode ? L"笔记已开启：输入数字来标记候选数。" : L"笔记已关闭：输入数字来填写答案。",0);
    } else if (id==31) erase_value();
    else if (id==32) undo();
    else if (id==33) hint();
    else if (id==34 && allow_new_game()) new_game();
    repaint();
}

static int hit_test(int x, int y) {
    int i;
    for (i=0;i<3;++i) if (inside(x,y,40+i*76,128,66,30)) return 20+i;
    for (i=0;i<9;++i)
        if (inside(x,y,616+(i%3)*104,268+(i/3)*78,94,68)) return i+1;
    for (i=0;i<3;++i) if (inside(x,y,616+i*104,520,94,44)) return 30+i;
    if (inside(x,y,616,578,302,44)) return 33;
    if (inside(x,y,616,636,302,44)) return 34;
    return 0;
}

static void draw_board(HDC dc) {
    int i, row, col, selected_value = selected>=0 ? game.values[selected] : 0;
    HPEN thin, thick;
    HGDIOBJ old_pen;
    for (i=0;i<81;++i) {
        int x=40+(i%9)*58, y=178+(i/9)*58, bad=sudoku_conflict(game.values,i);
        COLORREF bg=WHITE;
        wchar_t number[2]={0,0};
        if (selected>=0 && (i/9==selected/9 || i%9==selected%9 ||
                (i/27==selected/27 && (i%9)/3==(selected%9)/3))) bg=RGB(246,248,252);
        if (selected_value && game.values[i]==selected_value) bg=RGB(233,239,251);
        if (bad) bg=RGB(255,237,238);
        if (i==selected) bg=bad ? RGB(255,217,222) : RGB(216,231,255);
        fill(dc,x,y,58,58,bg);
        if (game.values[i]) {
            number[0]=L'0'+game.values[i];
            text(dc,number,x,y-1,58,58,4,bad ? RED : (puzzle.given[i] ? INK : BLUE),DT_CENTER);
        } else if (game.notes[i]) {
            int n;
            for (n=1;n<=9;++n) if (game.notes[i] & (1u<<n)) {
                number[0]=L'0'+n;
                text(dc,number,x+3+((n-1)%3)*17,y+3+((n-1)/3)*17,17,17,0,MUTED,DT_CENTER);
            }
        }
    }
    thin=CreatePen(PS_SOLID,length(1),LINE);
    thick=CreatePen(PS_SOLID,length(2),RGB(126,144,169));
    old_pen=SelectObject(dc,thin);
    for (i=0;i<=9;++i) {
        SelectObject(dc,i%3 ? thin : thick);
        MoveToEx(dc,sx(40+i*58),sy(178),0); LineTo(dc,sx(40+i*58),sy(700));
        MoveToEx(dc,sx(40),sy(178+i*58),0); LineTo(dc,sx(562),sy(178+i*58));
    }
    SelectObject(dc,old_pen); DeleteObject(thin); DeleteObject(thick);
    if (selected>=0) {
        HPEN pen=CreatePen(PS_SOLID,length(2),BLUE);
        HGDIOBJ op=SelectObject(dc,pen), ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        row=selected/9; col=selected%9;
        Rectangle(dc,sx(41+col*58),sy(179+row*58),sx(40+(col+1)*58),sy(178+(row+1)*58));
        SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pen);
    }
}

static void draw_timer(HDC dc) {
    wchar_t timer[32];
    unsigned seconds=(unsigned)((finished ? elapsed : ticks()-started)/1000);
    fill(dc,708,63,145,36,WHITE);
    if (seconds<3600) wsprintfW(timer,L"%02u:%02u",seconds/60,seconds%60);
    else wsprintfW(timer,L"%u:%02u:%02u",seconds/3600,(seconds/60)%60,seconds%60);
    text(dc,timer,710,63,143,36,6,INK,DT_LEFT);
}

static void draw(HDC dc) {
    int i, filled=0;
    wchar_t label[96];
    RECT all={0,0,buffer_w,buffer_h};
    brush_color(dc,BG); FillRect(dc,&all,(HBRUSH)GetStockObject(DC_BRUSH));
    text(dc,L"S U D O K U  /  v1.1",40,20,400,22,0,BLUE,DT_LEFT);
    text(dc,L"一个简单快速的数独",38,46,610,42,5,INK,DT_LEFT);
    text(dc,L"专注一格，解开整局。",40,91,400,24,1,MUTED,DT_LEFT);
    for (i=0;i<81;++i) if (game.values[i]) ++filled;
    rounded(dc,692,36,248,76,12,WHITE,LINE);
    text(dc,L"用时",712,43,120,20,0,MUTED,DT_LEFT);
    draw_timer(dc);
    wsprintfW(label,L"%d / 81",filled);
    text(dc,L"已填写",850,43,76,20,0,MUTED,DT_CENTER);
    text(dc,label,839,69,89,26,2,finished ? GREEN : INK,DT_CENTER);
    for (i=0;i<3;++i) button(dc,20+i,40+i*76,128,66,30,levels[i],i==difficulty,0);
    wsprintfW(label,L"%d 个已知数  /  唯一解",puzzle.clues);
    text(dc,label,302,128,260,30,7,MUTED,DT_RIGHT);
    draw_board(dc);
    rounded(dc,594,178,346,522,14,WHITE,LINE);
    text(dc,finished ? L"漂亮，解开了！" : L"填入数字",616,198,302,30,3,finished ? GREEN : INK,DT_LEFT);
    text(dc,note_mode ? L"笔记模式 · 标记你的候选数" : L"点击数字，或使用键盘 1—9",616,234,302,22,7,MUTED,DT_LEFT);
    for (i=0;i<9;++i) {
        int j, remaining=9, x=616+i%3*104, y=268+i/3*78;
        wchar_t digit[2]={(wchar_t)(L'1'+i),0};
        for (j=0;j<81;++j) if (game.values[j]==i+1) --remaining;
        rounded(dc,x,y,94,68,10,hover==i+1 ? PALE : RGB(248,250,253),hover==i+1 ? RGB(181,202,246) : RGB(233,237,244));
        text(dc,digit,x,y+2,94,40,4,remaining<=0 ? MUTED : BLUE,DT_CENTER);
        wsprintfW(label,L"剩余 %d",remaining>0 ? remaining : 0);
        text(dc,label,x,y+43,94,18,0,MUTED,DT_CENTER);
    }
    button(dc,30,616,520,94,44,note_mode ? L"笔记：开" : L"笔记：关",note_mode,0);
    button(dc,31,720,520,94,44,L"擦除",0,0);
    button(dc,32,824,520,94,44,L"撤销",0,0);
    button(dc,33,616,578,302,44,L"给我一个提示",0,0);
    button(dc,34,616,636,302,44,L"新的一局",0,1);
    text(dc,L"方向键移动   /   空格：笔记   /   Ctrl+Z：撤销   /   F2：新题",40,716,900,26,7,MUTED,DT_LEFT);
    text(dc,status_text,40,750,900,28,1,status_kind==1 ? GREEN : (status_kind==2 ? RED : MUTED),DT_LEFT);
}

static void resize_buffer(int width, int height) {
    HDC dc;
    double next_scale;
    if (width<1 || height<1) return;
    next_scale=(double)width/APP_W;
    if ((double)height/APP_H<next_scale) next_scale=(double)height/APP_H;
    scale=next_scale;
    offset_x=(width-(int)(APP_W*scale))/2;
    offset_y=(height-(int)(APP_H*scale))/2;
    make_fonts();
    if (buffer_w==width && buffer_h==height) return;
    dc=GetDC(window);
    if (!buffer_dc) buffer_dc=CreateCompatibleDC(dc);
    if (buffer_bitmap) {
        SelectObject(buffer_dc,buffer_original); DeleteObject(buffer_bitmap);
    }
    buffer_bitmap=CreateCompatibleBitmap(dc,width,height);
    buffer_original=SelectObject(buffer_dc,buffer_bitmap);
    buffer_w=width; buffer_h=height;
    ReleaseDC(window,dc);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        window=hwnd; new_game(); SetTimer(hwnd,1,1000,0); return 0;
    case WM_SIZE:
        if (wp!=SIZE_MINIMIZED) { resize_buffer(LOWORD(lp),HIWORD(lp)); repaint(); }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        if (buffer_dc && buffer_bitmap) {
            RECT timer_area=rect(708,63,145,36);
            if (ps.rcPaint.left>=timer_area.left && ps.rcPaint.top>=timer_area.top &&
                ps.rcPaint.right<=timer_area.right && ps.rcPaint.bottom<=timer_area.bottom)
                draw_timer(buffer_dc);
            else draw(buffer_dc);
            BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,
                ps.rcPaint.bottom-ps.rcPaint.top,buffer_dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_TIMER:
        if (!finished && !IsIconic(hwnd)) {
            RECT area=rect(708,63,145,36); InvalidateRect(hwnd,&area,FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x=(int)((GET_X_LPARAM(lp)-offset_x)/scale), y=(int)((GET_Y_LPARAM(lp)-offset_y)/scale);
        int id=hit_test(x,y);
        SetFocus(hwnd);
        if (inside(x,y,40,178,522,522)) {
            selected=((y-178)/58)*9+(x-40)/58; repaint();
        } else if (id) action(id);
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT track;
        int x=(int)((GET_X_LPARAM(lp)-offset_x)/scale), y=(int)((GET_Y_LPARAM(lp)-offset_y)/scale);
        int next=hit_test(x,y);
        if (hover!=next) { hover=next; repaint(); }
        SetCursor(LoadCursorW(0,next ? IDC_HAND : IDC_ARROW));
        memset(&track,0,sizeof(track)); track.cbSize=sizeof(track); track.dwFlags=TME_LEAVE; track.hwndTrack=hwnd;
        TrackMouseEvent(&track); return 0;
    }
    case WM_MOUSELEAVE: hover=0; repaint(); return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL)&0x8000) && wp=='Z') { undo(); return 0; }
        if ((GetKeyState(VK_CONTROL)&0x8000) && wp=='N') { action(34); return 0; }
        if (wp==VK_F2) { action(34); return 0; }
        if (wp==VK_DELETE) { erase_value(); return 0; }
        if (wp==VK_LEFT || wp==VK_RIGHT || wp==VK_UP || wp==VK_DOWN || wp==VK_TAB) {
            if (selected<0) selected=0;
            else if (wp==VK_LEFT) selected=selected/9*9+(selected%9+8)%9;
            else if (wp==VK_RIGHT) selected=selected/9*9+(selected%9+1)%9;
            else if (wp==VK_UP) selected=(selected+72)%81;
            else if (wp==VK_DOWN) selected=(selected+9)%81;
            else selected=(selected+((GetKeyState(VK_SHIFT)&0x8000) ? 80 : 1))%81;
            repaint(); return 0;
        }
        break;
    case WM_CHAR:
        if (wp>=L'1' && wp<=L'9') enter_value((int)(wp-L'0'),0);
        else if (wp==L'0' || wp==L'\b') erase_value();
        else if (wp==L' ' || wp==L'p' || wp==L'P') action(30);
        else if (wp==L'h' || wp==L'H') hint();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info=(MINMAXINFO *)lp;
        info->ptMinTrackSize.x=760; info->ptMinTrackSize.y=660; return 0;
    }
    case 0x02E0: { /* WM_DPICHANGED */
        RECT *suggested=(RECT *)lp;
        SetWindowPos(hwnd,0,suggested->left,suggested->top,suggested->right-suggested->left,
            suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0;
    }
    case WM_DESTROY: {
        int i;
        KillTimer(hwnd,1);
        if (buffer_bitmap) { SelectObject(buffer_dc,buffer_original); DeleteObject(buffer_bitmap); }
        if (buffer_dc) DeleteDC(buffer_dc);
        for (i=0;i<8;++i) if (fonts[i]) DeleteObject(fonts[i]);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    WNDCLASSEXW wc;
    MSG message;
    RECT work, bounds;
    HMODULE user=GetModuleHandleW(L"user32.dll"), advapi;
    typedef BOOL (WINAPI *DpiContext)(HANDLE);
    typedef BOOL (WINAPI *RandomBytes)(void *,ULONG);
    DpiContext set_dpi=(DpiContext)GetProcAddress(user,"SetProcessDpiAwarenessContext");
    RandomBytes random_bytes;
    int width, height, available_w, available_h;
    LARGE_INTEGER counter;
    (void)previous; (void)command;
    if (set_dpi) set_dpi((HANDLE)(INT_PTR)-4);
    else {
        typedef BOOL (WINAPI *DpiAware)(void);
        DpiAware fallback=(DpiAware)GetProcAddress(user,"SetProcessDPIAware");
        if (fallback) fallback();
    }
    QueryPerformanceCounter(&counter);
    rng=(uint32_t)counter.QuadPart ^ (uint32_t)ticks() ^ GetCurrentProcessId();
    advapi=LoadLibraryW(L"advapi32.dll");
    if (advapi) {
        random_bytes=(RandomBytes)GetProcAddress(advapi,"SystemFunction036");
        if (random_bytes) random_bytes(&rng,sizeof(rng));
        FreeLibrary(advapi);
    }
    memset(&wc,0,sizeof(wc)); wc.cbSize=sizeof(wc);
    wc.lpfnWndProc=window_proc; wc.hInstance=instance;
    wc.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);
    wc.hIconSm=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,16,16,LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon=LoadIconW(0,IDI_APPLICATION);
    wc.hCursor=LoadCursorW(0,IDC_ARROW);
    wc.lpszClassName=L"LeanSudokuWindow";
    if (!RegisterClassExW(&wc)) return 1;
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    available_w=work.right-work.left; available_h=work.bottom-work.top;
    width=APP_W; height=APP_H;
    if (height>available_h-90) { height=available_h-90; width=height*APP_W/APP_H; }
    if (width>available_w-60) { width=available_w-60; height=width*APP_H/APP_W; }
    bounds.left=0; bounds.top=0; bounds.right=width; bounds.bottom=height;
    AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0);
    width=bounds.right-bounds.left; height=bounds.bottom-bounds.top;
    window=CreateWindowExW(0,wc.lpszClassName,L"一个简单快速的数独 v1.1",WS_OVERLAPPEDWINDOW,
        work.left+(available_w-width)/2,work.top+(available_h-height)/2,width,height,
        0,0,instance,0);
    if (!window) return 1;
    ShowWindow(window,show); UpdateWindow(window);
    while (GetMessageW(&message,0,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return (int)message.wParam;
}
