/*
 * mini_make.c - Minimal Make utility for basic Makefile processing (C90)
 * Supports:
 *   - Basic targets with dependencies
 *   - Simple variable assignment and substitution
 *   - Command execution for targets
 *   - Ignores advanced make features (no pattern rules, no wildcards, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

#define MAX_LINE 1024
#define MAX_TARGETS 128
#define MAX_DEPS 32
#define MAX_CMDS 16
#define MAX_VARS 64
#define MAX_VAR_NAME 32
#define MAX_VAR_VAL 256

struct Variable {
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VAL];
};

struct Target {
    char name[MAX_LINE];
    char dependencies[MAX_DEPS][MAX_LINE];
    int dep_count;
    char commands[MAX_CMDS][MAX_LINE];
    int cmd_count;
};

static struct Variable vars[MAX_VARS];
static int var_count = 0;

static struct Target targets[MAX_TARGETS];
static int target_count = 0;

/* Helper: Trim leading and trailing whitespace */
void trim(char *s) {
    int i, j = 0;
    while (isspace((unsigned char)s[0])) ++s;
    for (i = 0; s[i] && s[i] != '\n' && s[i] != '\r'; ++i)
        ;
    s[i] = '\0';
    while (i > 0 && isspace((unsigned char)s[i-1]))
        s[--i] = '\0';
}

/* Helper: Find variable value */
const char *get_var(const char *name) {
    int i;
    for (i = 0; i < var_count; ++i) {
        if (strcmp(name, vars[i].name) == 0)
            return vars[i].value;
    }
    return "";
}

/* Helper: Replace $(VAR) in a line with its value */
void expand_vars(const char *src, char *dst, int dst_size) {
    int i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '$' && src[i+1] == '(') {
            int k = i+2, varlen = 0;
            char varname[MAX_VAR_NAME];
            while (src[k] && src[k] != ')' && varlen < MAX_VAR_NAME-1)
                varname[varlen++] = src[k++];
            varname[varlen] = '\0';
            if (src[k] == ')') ++k;
            strncpy(dst+j, get_var(varname), dst_size-j-1);
            j += strlen(get_var(varname));
            i = k;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

/* Helper: Parse variable assignment */
int parse_var(char *line) {
    char *eq = strchr(line, '=');
    if (!eq) return 0;
    char vname[MAX_VAR_NAME], vval[MAX_VAR_VAL];
    int n = eq - line;
    strncpy(vname, line, n); vname[n] = '\0';
    strcpy(vval, eq+1);
    trim(vname); trim(vval);
    if (var_count < MAX_VARS) {
        strcpy(vars[var_count].name, vname);
        strcpy(vars[var_count].value, vval);
        ++var_count;
    }
    return 1;
}

/* Helper: Parse target line */
int parse_target(char *line, struct Target *tgt) {
    char *colon = strchr(line, ':');
    int i, n;
    if (!colon) return 0;
    n = colon - line;
    strncpy(tgt->name, line, n); tgt->name[n] = '\0';
    trim(tgt->name);
    tgt->dep_count = 0;
    tgt->cmd_count = 0;
    char *deps = colon + 1;
    char *d = strtok(deps, " \t\n\r");
    while (d && tgt->dep_count < MAX_DEPS) {
        strcpy(tgt->dependencies[tgt->dep_count++], d);
        d = strtok(NULL, " \t\n\r");
    }
    return 1;
}

/* Helper: Find target by name */
struct Target *find_target(const char *name) {
    int i;
    for (i = 0; i < target_count; ++i) {
        if (strcmp(targets[i].name, name) == 0)
            return &targets[i];
    }
    return NULL;
}

/* Helper: File modification time. Returns -1 if file doesn't exist. */
long file_mtime(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) return (long)st.st_mtime;
    return -1L;
}

/* Helper: Build target, recursively */
int build_target(const char *name) {
    struct Target *tgt = find_target(name);
    int i;
    long tgt_time, dep_time, latest_dep = 0;
    char expanded[MAX_LINE];

    if (!tgt) {
        /* No rule: try to see if it's a file that exists */
        if (access(name, 0) == 0) return 0;
        printf("mini_make: *** No rule to make target '%s'. Stop.\n", name);
        return 1;
    }

    tgt_time = file_mtime(tgt->name);
    for (i = 0; i < tgt->dep_count; ++i) {
        if (build_target(tgt->dependencies[i])) return 1;
        dep_time = file_mtime(tgt->dependencies[i]);
        if (dep_time > latest_dep) latest_dep = dep_time;
    }

    if (tgt_time < latest_dep || tgt_time == -1L) {
        for (i = 0; i < tgt->cmd_count; ++i) {
            expand_vars(tgt->commands[i], expanded, sizeof(expanded));
            printf("%s\n", expanded);
            if (system(expanded) != 0) {
                printf("mini_make: *** Command failed: %s\n", expanded);
                return 1;
            }
        }
    }
    return 0;
}

/* Parse the Makefile */
void parse_makefile(const char *fname) {
    FILE *f = fopen(fname, "r");
    char line[MAX_LINE];
    struct Target *curr = NULL;
    if (!f) {
        printf("mini_make: Cannot open %s\n", fname);
        exit(1);
    }
    while (fgets(line, sizeof(line), f)) {
        char *pline = line;
        trim(pline);
        if (pline[0] == '\0' || pline[0] == '#') continue;
        if (strchr(pline, '=') && !isspace((unsigned char)pline[0])) {
            parse_var(pline);
        } else if (strchr(pline, ':')) {
            if (target_count < MAX_TARGETS) {
                if (parse_target(pline, &targets[target_count]))
                    curr = &targets[target_count++];
                else
                    curr = NULL;
            }
        } else if (pline[0] == '\t' && curr) {
            if (curr->cmd_count < MAX_CMDS) {
                strcpy(curr->commands[curr->cmd_count++], pline+1);
            }
        } else {
            curr = NULL;
        }
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    const char *makefile = "Makefile";
    const char *target = NULL;
    int i;

    /* Look for -f Makefile option */
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-f") == 0 && i+1 < argc)
            makefile = argv[++i];
        else if (argv[i][0] != '-')
            target = argv[i];
    }

    parse_makefile(makefile);
    if (target == NULL) {
        if (target_count > 0)
            target = targets[0].name;
        else {
            printf("mini_make: No targets found.\n");
            return 1;
        }
    }
    return build_target(target);
}