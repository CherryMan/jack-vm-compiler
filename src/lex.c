#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex.h"

/**
 * Conversion tables.
 *
 * Convert from string tokens to their enum equivalent.
 * This makes parsing easier once everything is tokenized.
 *
 */

static const struct {
    char *key;
    CommandType val;
} command[] = {
    {"push",     PUSH       },
    {"pop",      POP        },
    {"label",    LABEL      },
    {"goto",     GOTO       },
    {"if-goto",  IF         },
    {"function", FUNCTION   },
    {"return",   RETURN     },
    {"call",     CALL       },
    {"add",      ARITHMETIC },
    {"sub",      ARITHMETIC },
    {"neg",      ARITHMETIC },
    {"eq",       ARITHMETIC },
    {"gt",       ARITHMETIC },
    {"lt",       ARITHMETIC },
    {"and",      ARITHMETIC },
    {"or",       ARITHMETIC },
    {"not",      ARITHMETIC },
};

static const struct {
    char *key;
    Memory val;
} memory[] = {
    {"argument", ARGUMENT },
    {"local",    LOCAL    },
    {"static",   STATIC   },
    {"constant", CONSTANT },
    {"this",     THIS     },
    {"that",     THAT     },
    {"pointer",  POINTER  },
    {"temp",     TEMP     },
};

static const struct {
    char *key;
    RType val;
} arithmetic[] = {
    {"add", ADD },
    {"sub", SUB },
    {"neg", NEG },
    {"eq",  EQ  },
    {"gt",  GT  },
    {"lt",  LT  },
    {"and", AND },
    {"or",  OR  },
    {"not", NOT },
};

static const struct CommandFormat {
    int nargs;
    CmdArgType arg[3];
} cmd_fmt[] = {
    [NONE]       = { 0 },
    [ARITHMETIC] = { 1, { ARG_CMD } },
    [PUSH]       = { 3, { ARG_NONE, ARG_MEMORY, ARG_NUM } },
    [POP]        = { 3, { ARG_NONE, ARG_MEMORY, ARG_NUM } },
    [LABEL]      = { 2, { ARG_NONE, ARG_NAME } },
    [GOTO]       = { 2, { ARG_NONE, ARG_NAME } },
    [IF]         = { 2, { ARG_NONE, ARG_NAME } },
    [FUNCTION]   = { 3, { ARG_NONE, ARG_NAME, ARG_NUM } },
    [CALL]       = { 3, { ARG_NONE, ARG_NAME, ARG_NUM } },
    [RETURN]     = { 0 },
};


static char *nextline(FILE*);
static CommandType cmdtype(char*);


TokenList *new_token_list() {
    TokenList *r = malloc(sizeof(TokenList));

    if (!r) {
        fprintf(stderr, "Failed to allocate TokenList\n");
        exit(1);
    }

    r->cmd = NONE;
    r->argc = 0;
    r->argv = NULL;
    r->next = NULL;

    return r;
}

void free_token_list(TokenList *tl) {
    TokenList *n;

    if (tl) {
        n = tl->next;

        free(tl->argv);
        free(tl);

        if (n)
            free_token_list(n);
    }
}

TokenList *scan_stream(FILE *fp) {

    TokenList *r = NULL;
    TokenList *curr = NULL;
    TokenList *prev = NULL;

    CommandType cmdt;
    struct CommandFormat fmt;

    int argc = 0;
    CmdArg *argv = NULL;

    char *tokdelim = " \t";
    int failure = 0;

    char *line, *cmd, *nword;
    while ((line = nextline(fp))) {

        cmd = strtok(line, tokdelim); // FIXME?: null return
        cmdt = cmdtype(cmd);

        if (cmdt == NONE) {
            fprintf(stderr, "Unknown command '%s'\n", cmd);
            continue;
        }

        int argn;
        fmt = cmd_fmt[cmdt];

        if (fmt.arg[0] == ARG_NONE) {

            argn = 0;
            argc = fmt.nargs - 1;
            argv = malloc(argc * sizeof(CmdArg));

        } else {

            argn = 1;
            argc = fmt.nargs;
            argv = malloc(argc * sizeof(CmdArg));

            switch (fmt.arg[0]) {
                int s, j;

                case ARG_CMD:

                    s = sizeof(arithmetic) / sizeof(arithmetic[0]);
                    for (j = 0; j < s; ++j) {
                        if (strcmp(cmd, arithmetic[j].key) == 0) {
                            argv[0].op = arithmetic[j].val;
                            break;
                        }
                    }

                    break;

                default:
                    /* nop */
                    break;
            }
        }

        for (int i = 1; i < fmt.nargs; ++i, ++argn) {
            nword = strtok(NULL, tokdelim);

            if (!nword) {
                fprintf(stderr,
                        "Missing token at line '%s'\n", line);
                failure = 1;
                continue;
            }

            switch (fmt.arg[i]) {
                int s;
                int found;
                int j;

                char *end, *name;
                long num;

                case ARG_MEMORY:
                    s = sizeof(memory) / sizeof(memory[0]);
                    found = 0;
                    for (j = 0; j < s; ++j) {
                        if (strcmp(nword, memory[j].key) == 0) {
                            argv[argn].mem = memory[j].val;
                            found = 1;
                            break;
                        }
                    }

                    if (cmdt == POP && argv[argn].mem == CONSTANT) {
                        fprintf(stderr, "Cannot call POP on constant segment\n");
                        failure = 1;
                    }

                    // If no matching memory segment is found
                    if (!found) {
                        fprintf(stderr, "Invalid memory segment '%s'\n", nword);
                        failure = 1;
                    }

                    break;

                case ARG_NUM:
                    num = strtoll(nword, &end, 10);
                    if (errno == ERANGE || end == nword) {
                        fprintf(stderr, "Failed to read number '%s' in line '%s'", nword, line);
                        failure = 1;
                    }

                    // If command type is POP or PUSH,
                    // check memory segment limits
                    int num_is_invalid = 0;
                    if (cmdt & (POP | PUSH)) {
                        switch(argv[argn-1].mem) {
                            case TEMP:
                                if (num < 0 || num > 7) {
                                    fprintf(stderr, "Temporary segment %ld is not between 0 and 7\n", num);
                                    num_is_invalid = 1;
                                }
                                break;

                            default:
                                if (num < 0 || num > 32767) {
                                    fprintf(stderr, "Constant segment %ld is not between 0 and 32767\n", num);
                                    num_is_invalid = 1;
                                }
                                break;
                        }
                    }

                    failure |= num_is_invalid; // if (num_is_invalid) failure = true;

                    argv[argn].num = (int) num;
                    break;

                case ARG_NAME:
                    name = malloc(strlen(nword) * sizeof(char));
                    strcpy(name, nword);

                    argv[argn].name = name;
                    break;

                default:
                    /* nop */
                    break;
            }
        }

        if (!failure) {
            curr = new_token_list();
            curr->cmd  = cmdt;
            curr->argc = argc;
            curr->argv = argv;

            if (prev)
                prev->next = curr;

            prev = curr;

            if (!r)
                r = prev;
        }

        free(line);
    }

    if (failure) {
        fprintf(stderr,
                "Failed to compile\n");
        exit(1);
    }

    return r;
}


char *nextline(FILE *fp) {

    // Return value and size
    int rs = 0;
    char *r = NULL;

    // For reallocation
    char *new = NULL;

    int c;
    int i = 0;

    if (feof(fp))
        return NULL;

    // Remove leading whitespace
    while (isspace(c = fgetc(fp)))
        ; /* NOP */
    ungetc(c, fp);

    while ((c = fgetc(fp)) != EOF) {

        if (c == '\n')
            break;

        // Check for comments
        if (c == '/') {

            int n;
            if ((n = fgetc(fp)) == '/') {

                while(fgetc(fp) != '\n')
                    ; /* NOP */

                break;

            } else {
                ungetc(n, fp);
            }
        }

        // Reallocate if necessary
        if (rs <= i + 1) {
            rs += 16;

            if (r)
                new = realloc(r, rs);
            else
                new = malloc(rs * sizeof(char));

            if (new) {
                r = new;
            } else {
                fprintf(stderr, "Failed to allocate memory\n");
                exit(1);
            }
        }

        r[i++] = c;
    }

    if (r) {
        r[i] = '\0';
        r = realloc(r, i + 1);

        return r;

    } else {
        return nextline(fp);
    }
}


CommandType cmdtype(char* cmd) {
    CommandType r = NONE;

    int s = sizeof(command) / sizeof(command[0]);
    int i;
    for (i = 0; i < s; ++i) {
        if (strcmp(cmd, command[i].key) == 0) {
            r = command[i].val;
        }
    }

    return r;
}
