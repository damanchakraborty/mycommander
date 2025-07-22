#define _XOPEN_SOURCE 600
#include <ncurses.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>

#define MAX_FILES 4096
#define PATH_MAX_LEN 4096

#define MIN_WIDTH  60
#define MIN_HEIGHT 10

typedef enum {
    TYPE_FOLDER,
    TYPE_TEXT,
    TYPE_EXEC,
    TYPE_IMAGE,
    TYPE_VIDEO,
    TYPE_OTHER
} FileType;

typedef struct {
    char *name;
    FileType type;
} Entry;

typedef struct {
    Entry entries[MAX_FILES];
    int count;
    int selected;
    int scroll_offset;
    char cwd[PATH_MAX_LEN];
} Panel;

volatile sig_atomic_t resized = 0;
void handle_winch(int sig) { resized = 1; }

FileType detect_file_type(const char *path, struct stat *st) {
    if (S_ISDIR(st->st_mode)) return TYPE_FOLDER;
    if (st->st_mode & S_IXUSR) return TYPE_EXEC;
    const char *ext = strrchr(path, '.');
    if (ext) {
        if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0) return TYPE_TEXT;
        if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0) return TYPE_IMAGE;
        if (strcmp(ext, ".mp4") == 0 || strcmp(ext, ".mkv") == 0) return TYPE_VIDEO;
    }
    return TYPE_OTHER;
}

int compare_entries(const void *a, const void *b) {
    Entry *ea = (Entry *)a;
    Entry *eb = (Entry *)b;
    if (ea->type == TYPE_FOLDER && eb->type != TYPE_FOLDER) return -1;
    if (ea->type != TYPE_FOLDER && eb->type == TYPE_FOLDER) return 1;
    return strcmp(ea->name, eb->name);
}

void list_dir(Panel *panel) {
    DIR *dir = opendir(panel->cwd);
    if (!dir) return;

    panel->count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && panel->count < MAX_FILES) {
        panel->entries[panel->count].name = strdup(entry->d_name);
        char full[PATH_MAX_LEN];
        snprintf(full, PATH_MAX_LEN, "%s/%s", panel->cwd, entry->d_name);
        struct stat st;
        if (stat(full, &st) == 0)
            panel->entries[panel->count].type = detect_file_type(full, &st);
        else panel->entries[panel->count].type = TYPE_OTHER;
        panel->count++;
    }
    closedir(dir);
    qsort(panel->entries, panel->count, sizeof(Entry), compare_entries);
}

void free_panel(Panel *panel) {
    for (int i = 0; i < panel->count; i++) free(panel->entries[i].name);
}

void draw_panel(WINDOW *win, Panel *panel, int active) {
    werase(win); box(win,0,0);
    mvwprintw(win,0,2,"[ %s ]",panel->cwd);
    int h,w; getmaxyx(win,h,w);
    int list_h = h-2;
    if (panel->selected < panel->scroll_offset) panel->scroll_offset = panel->selected;
    if (panel->selected >= panel->scroll_offset + list_h) panel->scroll_offset = panel->selected - list_h + 1;
    for (int i=0;i<list_h;i++) {
        int idx = panel->scroll_offset + i;
        if (idx >= panel->count) break;
        if (idx == panel->selected) wattron(win,A_REVERSE | (active?A_BOLD:0));
        const char *icon = "";
        switch(panel->entries[idx].type) {
            case TYPE_FOLDER: icon = "[DIR]"; break;
            case TYPE_TEXT: icon = "[TXT]"; break;
            case TYPE_EXEC: icon = "[EXE]"; break;
            case TYPE_IMAGE: icon = "[IMG]"; break;
            case TYPE_VIDEO: icon = "[VID]"; break;
            default: icon = "[OTH]"; break;
        }
        if (panel->entries[idx].type == TYPE_FOLDER)
            mvwprintw(win,i+1,1,"%-6s /%s",icon,panel->entries[idx].name);
        else
            mvwprintw(win,i+1,1,"%-6s %s",icon,panel->entries[idx].name);
        if (idx == panel->selected) wattroff(win,A_REVERSE | (active?A_BOLD:0));
    }
    wrefresh(win);
}

void draw_terminal(WINDOW *win, char *input) {
    werase(win); box(win,0,0);
    int h,w; getmaxyx(win,h,w);
    mvwprintw(win,0,2,"[ Terminal Input ]");
    mvwprintw(win,1,1,"> %s",input);
    wrefresh(win);
}

void open_entry(Panel *p) {
    char *sel = p->entries[p->selected].name;
    if (!strcmp(sel,".")) return;
    if (!strcmp(sel,"..")) chdir("..");
    else {
        Entry *e = &p->entries[p->selected];
        if (e->type == TYPE_FOLDER) {
            chdir(sel);
        } else if (e->type == TYPE_TEXT) {
            endwin();
            char cmd[PATH_MAX_LEN + 32];
            snprintf(cmd, sizeof(cmd), "nano \"%s\"", sel);
            system(cmd);
            initscr(); noecho(); curs_set(0); keypad(stdscr,1);
            resized = 1;
        } else {
            if (fork() == 0) {
                char cmd[PATH_MAX_LEN + 64];
                snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" > /dev/null 2>&1", sel);
                execlp("sh", "sh", "-c", cmd, NULL);
                exit(1);
            }
        }
    }
    getcwd(p->cwd, PATH_MAX_LEN); free_panel(p); list_dir(p);
    p->selected = p->scroll_offset = 0;
}

int main() {
    signal(SIGWINCH, handle_winch);

    Panel l,r; getcwd(l.cwd,PATH_MAX_LEN); strcpy(r.cwd,"/");
    list_dir(&l); list_dir(&r);

    int h,w; initscr(); noecho(); curs_set(0); keypad(stdscr,1);
    getmaxyx(stdscr,h,w);

    const int terminal_height = 3;
    int ph = h - terminal_height;
    int th = terminal_height;

    WINDOW *lw = newwin(ph,w/2,0,0);
    WINDOW *rw = newwin(ph,w/2,0,w/2);
    WINDOW *tw = newwin(th,w,ph,0);

    enum {FOCUS_L, FOCUS_R} focus = FOCUS_L;

    char input[512]={0}; int ilen=0;

    draw_panel(lw,&l,focus==FOCUS_L);
    draw_panel(rw,&r,focus==FOCUS_R);
    draw_terminal(tw,input);

    while(1) {
        getmaxyx(stdscr,h,w);

        if (w < MIN_WIDTH || h < MIN_HEIGHT) {
            clear();
            const char *msg = "Window too small! Resize to continue.";
            mvprintw(h/2, (w - strlen(msg))/2, "%s", msg);
            refresh();
            int ch = getch();
            if (ch == 'q') break;
            continue;
        }

        int ch = getch();
        if (ch == 'q') break;

        if (ch == '\t') {
            focus = (focus == FOCUS_L) ? FOCUS_R : FOCUS_L;
        }
        else if (ch == KEY_UP || ch == KEY_DOWN) {
            if (focus == FOCUS_L) {
                if (ch == KEY_UP && l.selected > 0) l.selected--;
                if (ch == KEY_DOWN && l.selected < l.count - 1) l.selected++;
            } else {
                if (ch == KEY_UP && r.selected > 0) r.selected--;
                if (ch == KEY_DOWN && r.selected < r.count - 1) r.selected++;
            }
        }
        else if (ch == '\n') {
            if (ilen > 0) {
                endwin();
                Panel *p = (focus == FOCUS_L) ? &l : &r;
                chdir(p->cwd);
                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "bash -c '%s'", input);
                system(cmd);
                initscr(); noecho(); curs_set(0); keypad(stdscr,1);
                resized = 1; ilen = 0; input[0] = '\0';
            } else {
                if (focus == FOCUS_L) open_entry(&l);
                else open_entry(&r);
            }
        }
        else if (ch != ERR) {
            if (ch == 127 || ch == KEY_BACKSPACE) {
                if (ilen > 0) input[--ilen] = '\0';
            } else if (ch < 256 && ilen < sizeof(input)-1) {
                input[ilen++] = ch; input[ilen] = '\0';
            }
        }

        if (resized) {
            endwin(); refresh(); clear();
            getmaxyx(stdscr,h,w);
            if (w < MIN_WIDTH || h < MIN_HEIGHT) continue;

            ph = h - terminal_height;
            th = terminal_height;

            wresize(lw, ph, w/2);
            wresize(rw, ph, w/2);
            wresize(tw, th, w);

            mvwin(rw, 0, w/2);
            mvwin(tw, ph, 0);

            free_panel(&l); free_panel(&r);
            list_dir(&l); list_dir(&r);

            touchwin(stdscr); redrawwin(stdscr);
            touchwin(lw); wnoutrefresh(lw);
            touchwin(rw); wnoutrefresh(rw);
            touchwin(tw); wnoutrefresh(tw);
            doupdate();

            resized = 0;
        }

        draw_panel(lw,&l,focus==FOCUS_L);
        draw_panel(rw,&r,focus==FOCUS_R);
        draw_terminal(tw,input);
    }
    endwin();
    return 0;
}
