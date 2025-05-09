#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>

#define GAP_SIZE 256
#define LINE_CAP 4096
#define MAX_LINES 4096
#define FILENAME_MAXLEN 256

typedef enum { MODE_INSERT, MODE_COMMAND, MODE_NORMAL, MODE_SEARCH } EditorMode;

typedef struct {
    char *buf;
    int gap_start, gap_end, buf_size;
} GapBuf;

typedef struct {
    GapBuf **lines;
    int num_lines, cap_lines;
    int cx, cy;
    EditorMode mode;
    char command[128];
    char search[128];
    int search_last_y;
    int search_found;
    char filename[FILENAME_MAXLEN];
} Editor;

static struct termios orig_termios;
static int raw_mode_enabled = 0;

void die(const char *s) { perror(s); exit(1); }

void disableRawMode(void) {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = 0;
    }
}
void enableRawMode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    raw_mode_enabled = 1;
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int get_terminal_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return 80;
    return ws.ws_col;
}
int get_terminal_height(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return 24;
    return ws.ws_row;
}

GapBuf *make_gapbuf(int cap) {
    GapBuf *gb = malloc(sizeof(GapBuf));
    gb->buf = calloc(1, cap);
    gb->buf_size = cap;
    gb->gap_start = 0;
    gb->gap_end = GAP_SIZE;
    if (gb->gap_end > gb->buf_size) gb->gap_end = gb->buf_size;
    return gb;
}
void free_gapbuf(GapBuf *gb) { if (gb) { free(gb->buf); free(gb); } }

void ensure_gap(GapBuf *gb, int min_gap) {
    int gap_len = gb->gap_end - gb->gap_start;
    if (gap_len >= min_gap) return;
    int new_size = gb->buf_size + GAP_SIZE;
    char *new_buf = calloc(1, new_size);
    memcpy(new_buf, gb->buf, gb->gap_start);
    memcpy(new_buf + gb->gap_start + GAP_SIZE, gb->buf + gb->gap_end, gb->buf_size - gb->gap_end);
    free(gb->buf);
    gb->buf = new_buf;
    gb->gap_end = gb->gap_start + GAP_SIZE;
    gb->buf_size = new_size;
}
void move_gap(GapBuf *gb, int pos) {
    if (pos < 0) pos = 0;
    int len = gb->gap_start + (gb->buf_size - gb->gap_end);
    if (pos > len) pos = len;
    if (pos < gb->gap_start) {
        int move = gb->gap_start - pos;
        memmove(gb->buf + gb->gap_end - move, gb->buf + pos, move);
        gb->gap_start -= move;
        gb->gap_end -= move;
    } else if (pos > gb->gap_start) {
        int move = pos - gb->gap_start;
        memmove(gb->buf + gb->gap_start, gb->buf + gb->gap_end, move);
        gb->gap_start += move;
        gb->gap_end += move;
    }
}
void gapbuf_insert(GapBuf *gb, int pos, char c) {
    int len = gb->gap_start + (gb->buf_size - gb->gap_end);
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    move_gap(gb, pos);
    ensure_gap(gb, 1);
    gb->buf[gb->gap_start++] = c;
}
void gapbuf_delete(GapBuf *gb, int pos) {
    int len = gb->gap_start + (gb->buf_size - gb->gap_end);
    if (pos <= 0 || pos > len) return;
    move_gap(gb, pos);
    if (gb->gap_start > 0) gb->gap_start--;
}
int gapbuf_length(GapBuf *gb) {
    return gb->gap_start + (gb->buf_size - gb->gap_end);
}
char gapbuf_get(GapBuf *gb, int i) {
    int len = gapbuf_length(gb);
    if (i < 0 || i >= len) return 0;
    if (i < gb->gap_start) return gb->buf[i];
    else return gb->buf[i + (gb->gap_end - gb->gap_start)];
}
void gapbuf_to_cstr(GapBuf *gb, char *out, size_t out_size) {
    int len = gapbuf_length(gb);
    if ((size_t)len >= out_size) len = out_size - 1;
    int i, j = 0;
    for (i = 0; i < len; ++i)
        out[j++] = gapbuf_get(gb, i);
    out[j] = '\0';
}

void insert_line(Editor *ed, int at) {
    for (int i = ed->num_lines; i > at; --i)
        ed->lines[i] = ed->lines[i-1];
    ed->lines[at] = make_gapbuf(LINE_CAP);
    ed->num_lines++;
}
void split_line(Editor *ed, int y, int x) {
    GapBuf *gb = ed->lines[y];
    int len = gapbuf_length(gb);
    if (x < 0) x = 0;
    if (x > len) x = len;
    insert_line(ed, y+1);
    GapBuf *next = ed->lines[y+1];
    for (int i = x; i < len; ++i)
        gapbuf_insert(next, gapbuf_length(next), gapbuf_get(gb, i));
    gb->gap_start = x;
    gb->gap_end = gb->buf_size;
}
void delete_line(Editor *ed, int at) {
    if (ed->num_lines <= 1) return;
    free_gapbuf(ed->lines[at]);
    for (int i = at; i < ed->num_lines-1; i++)
        ed->lines[i] = ed->lines[i+1];
    ed->lines[ed->num_lines-1] = NULL;
    --ed->num_lines;
}
void join_line(Editor *ed, int y) {
    GapBuf *gb = ed->lines[y];
    GapBuf *next = ed->lines[y+1];
    int nlen = gapbuf_length(next);
    for (int i = 0; i < nlen; ++i)
        gapbuf_insert(gb, gapbuf_length(gb), gapbuf_get(next, i));
    delete_line(ed, y+1);
}

void load_file(Editor *ed, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return;
    char linebuf[LINE_CAP];
    int lineno = 0;
    while (fgets(linebuf, LINE_CAP, fp) && lineno < ed->cap_lines) {
        int len = strlen(linebuf);
        if (len && linebuf[len-1] == '\n') linebuf[--len] = 0;
        if (!ed->lines[lineno])
            ed->lines[lineno] = make_gapbuf(LINE_CAP);
        for (int i = 0; i < len; ++i)
            gapbuf_insert(ed->lines[lineno], gapbuf_length(ed->lines[lineno]), linebuf[i]);
        lineno++;
        if (lineno < ed->cap_lines) ed->num_lines++;
    }
    fclose(fp);
}
void save_file(Editor *ed, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;
    char tmp[LINE_CAP*2];
    for (int i = 0; i < ed->num_lines; ++i) {
        if (!ed->lines[i]) continue;
        gapbuf_to_cstr(ed->lines[i], tmp, sizeof(tmp));
        fprintf(fp, "%s\n", tmp);
    }
    fclose(fp);
}

enum { KEY_NULL = 0, KEY_ARROW_LEFT = 1000, KEY_ARROW_RIGHT, KEY_ARROW_UP, KEY_ARROW_DOWN };

int read_key(void) {
    char c;
    ssize_t nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) == 0);
    if (nread == -1) die("read");
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return KEY_ARROW_UP;
                case 'B': return KEY_ARROW_DOWN;
                case 'C': return KEY_ARROW_RIGHT;
                case 'D': return KEY_ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return c;
}

void get_screen_cursor(Editor *ed, int *out_row, int *out_col, int termwidth) {
    int row = 0;
    for (int i = 0; i < ed->cy; ++i) {
        int linelen = ed->lines[i] ? gapbuf_length(ed->lines[i]) : 0;
        row += (linelen + termwidth - 1) / termwidth;
        if (row < 0) row = 0;
    }
    int col = ed->cx % termwidth;
    row += ed->cx / termwidth;
    *out_row = row;
    *out_col = col;
}
void draw(Editor *ed) {
    int termwidth = get_terminal_width();
    int termheight = get_terminal_height();
    char tmp[LINE_CAP*2];
    write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7);
    int screenrow = 0;
    for (int i = 0; i < ed->num_lines && screenrow < termheight-2; ++i) {
        if (!ed->lines[i]) {
            printf("[NULL LINE]\n");
            screenrow++;
            continue;
        }
        gapbuf_to_cstr(ed->lines[i], tmp, sizeof(tmp));
        int linelen = strlen(tmp);
        int start = 0;
        while (start < linelen && screenrow < termheight-2) {
            int seglen = linelen-start;
            if (seglen > termwidth)
                seglen = termwidth;
            if (ed->mode == MODE_SEARCH && ed->search[0]) {
                char *found = strstr(tmp+start, ed->search);
                int offset = found ? (found - (tmp+start)) : -1;
                if (offset >= 0 && offset < seglen) {
                    fwrite(tmp+start, 1, offset, stdout);
                    printf("\x1b[7m");
                    fwrite(tmp+start+offset, 1, strlen(ed->search), stdout);
                    printf("\x1b[0m");
                    fwrite(tmp+start+offset+strlen(ed->search), 1, seglen-offset-strlen(ed->search), stdout);
                } else {
                    fwrite(tmp+start, 1, seglen, stdout);
                }
            } else {
                fwrite(tmp+start, 1, seglen, stdout);
            }
            if (seglen < termwidth)
                printf("%*s", termwidth-seglen, "");
            putchar('\n');
            start += seglen;
            screenrow++;
        }
        if (linelen == 0 && screenrow < termheight-2) {
            putchar('\n');
            screenrow++;
        }
    }
    const char *mode_str = (ed->mode == MODE_INSERT) ? "INSERT"
                          : (ed->mode == MODE_COMMAND) ? "COMMAND"
                          : (ed->mode == MODE_NORMAL) ? "NORMAL"
                          : "SEARCH";
    printf("---- %s MODE ----", mode_str);
    if (ed->mode == MODE_COMMAND) printf(":%s", ed->command);
    if (ed->mode == MODE_SEARCH) printf("/%s", ed->search);
    int crow, ccol;
    get_screen_cursor(ed, &crow, &ccol, termwidth);
    if (crow >= termheight-1) crow = termheight-2;
    printf("\x1b[%d;%dH", crow+1, ccol+1);
    fflush(stdout);
}

// --------- CHANGED: c == '\n' to c == '\n' || c == '\r' everywhere ---------

void process_insert(Editor *ed, int c) {
    if (!ed->lines[ed->cy]) ed->lines[ed->cy] = make_gapbuf(LINE_CAP);
    int len = gapbuf_length(ed->lines[ed->cy]);
    if (c == 27) { // ESC
        ed->mode = MODE_NORMAL;
        return;
    }
    if (c == KEY_ARROW_LEFT) {
        if (ed->cx > 0) ed->cx--;
    } else if (c == KEY_ARROW_RIGHT) {
        if (ed->cx < len) ed->cx++;
    } else if (c == KEY_ARROW_DOWN && ed->cy < ed->num_lines-1) {
        ed->cy++;
        int nlen = gapbuf_length(ed->lines[ed->cy]);
        if (ed->cx > nlen) ed->cx = nlen;
    } else if (c == KEY_ARROW_UP && ed->cy > 0) {
        ed->cy--;
        int nlen = gapbuf_length(ed->lines[ed->cy]);
        if (ed->cx > nlen) ed->cx = nlen;
    } else if (c == 127 || c == 8) { // Backspace
        if (ed->cx > 0) {
            gapbuf_delete(ed->lines[ed->cy], ed->cx);
            ed->cx--;
        } else if (ed->cy > 0) {
            int prevlen = gapbuf_length(ed->lines[ed->cy-1]);
            join_line(ed, ed->cy-1);
            ed->cy--;
            ed->cx = prevlen;
        }
    } else if (c == '\n' || c == '\r') {
        split_line(ed, ed->cy, ed->cx);
        ed->cy++; ed->cx = 0;
    } else if (c >= 32 && c < 127) {
        gapbuf_insert(ed->lines[ed->cy], ed->cx, c);
        ed->cx++;
    }
}

void process_command(Editor *ed, int c) {
    int clen = strlen(ed->command);
    if (c == '\n' || c == '\r') {
        if (strcmp(ed->command, "w") == 0)
            save_file(ed, ed->filename);
        else if (strcmp(ed->command, "q") == 0) {
            disableRawMode();
            if (ed->lines) {
                for (int i = 0; i < ed->num_lines; ++i) free_gapbuf(ed->lines[i]);
                free(ed->lines);
            }
            exit(0);
        } else if (strcmp(ed->command, "wq") == 0) {
            save_file(ed, ed->filename);
            disableRawMode();
            if (ed->lines) {
                for (int i = 0; i < ed->num_lines; ++i) free_gapbuf(ed->lines[i]);
                free(ed->lines);
            }
            exit(0);
        }
        ed->mode = MODE_INSERT;
    } else if ((c == 127 || c == 8) && clen > 0) {
        ed->command[clen-1] = '\0';
    } else if (c == 27) { // ESC
        ed->mode = MODE_INSERT;
    } else if (clen < 120 && c != 27) {
        ed->command[clen] = c; ed->command[clen+1] = 0;
    }
}

void process_normal(Editor *ed, int c) {
    int len = gapbuf_length(ed->lines[ed->cy]);
    if (c == 'i') {
        ed->mode = MODE_INSERT;
    } else if (c == ':') {
        ed->mode = MODE_COMMAND; ed->command[0] = 0;
    } else if (c == '/') {
        ed->mode = MODE_SEARCH; ed->search[0] = 0; ed->search_found = 0; ed->search_last_y = ed->cy;
    } else if (c == KEY_ARROW_LEFT) {
        if (ed->cx > 0) ed->cx--;
    } else if (c == KEY_ARROW_RIGHT) {
        if (ed->cx < len) ed->cx++;
    } else if (c == KEY_ARROW_DOWN && ed->cy < ed->num_lines-1) {
        ed->cy++;
        int nlen = gapbuf_length(ed->lines[ed->cy]);
        if (ed->cx > nlen) ed->cx = nlen;
    } else if (c == KEY_ARROW_UP && ed->cy > 0) {
        ed->cy--;
        int nlen = gapbuf_length(ed->lines[ed->cy]);
        if (ed->cx > nlen) ed->cx = nlen;
    }
}

void process_search(Editor *ed, int c) {
    int slen = strlen(ed->search);
    if (c == '\n' || c == '\r') {
        int y = ed->cy;
        for (int i = 0; i < ed->num_lines; ++i) {
            int lineidx = (y + i) % ed->num_lines;
            char tmp[LINE_CAP*2];
            gapbuf_to_cstr(ed->lines[lineidx], tmp, sizeof(tmp));
            char *found = strstr(tmp, ed->search);
            if (found) {
                ed->cy = lineidx;
                ed->cx = found - tmp;
                ed->search_found = 1;
                ed->search_last_y = ed->cy;
                ed->mode = MODE_NORMAL;
                return;
            }
        }
        ed->search_found = 0;
        ed->mode = MODE_NORMAL;
    } else if ((c == 127 || c == 8) && slen > 0) {
        ed->search[slen-1] = '\0';
    } else if (c == 27) { // ESC
        ed->mode = MODE_NORMAL;
    } else if (slen < 120 && c != 27) {
        ed->search[slen] = c; ed->search[slen+1] = 0;
    }
}

int main(int argc, char *argv[]) {
    Editor ed;
    memset(&ed, 0, sizeof(ed));
    ed.lines = calloc(MAX_LINES, sizeof(GapBuf*));
    ed.cap_lines = MAX_LINES;
    ed.num_lines = 1;
    ed.lines[0] = make_gapbuf(LINE_CAP);
    ed.cx = ed.cy = 0;
    ed.mode = MODE_INSERT;
    ed.filename[0] = 0;
    if (argc > 1) {
        strncpy(ed.filename, argv[1], FILENAME_MAXLEN-1);
        load_file(&ed, ed.filename);
    } else {
        printf("Usage: %s [filename]\n", argc > 0 ? argv[0] : "editor");
    }
    enableRawMode();

    while (1) {
        draw(&ed);
        int c = read_key();
        if (ed.mode == MODE_INSERT) process_insert(&ed, c);
        else if (ed.mode == MODE_COMMAND) process_command(&ed, c);
        else if (ed.mode == MODE_NORMAL) process_normal(&ed, c);
        else if (ed.mode == MODE_SEARCH) process_search(&ed, c);
        int len = (ed.lines && ed.lines[ed.cy]) ? gapbuf_length(ed.lines[ed.cy]) : 0;
        if (ed.cx < 0) ed.cx = 0;
        if (ed.cx > len) ed.cx = len;
        if (ed.cy < 0) ed.cy = 0;
        if (ed.cy >= ed.num_lines) ed.cy = ed.num_lines-1;
    }
    if (ed.lines) {
        for (int i = 0; i < ed.num_lines; ++i) free_gapbuf(ed.lines[i]);
        free(ed.lines);
    }
    disableRawMode();
    return 0;
}
