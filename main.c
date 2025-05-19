#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>

typedef struct {
    char *data;
    size_t len;
} str;

char *str_chr(str haystack, char needle) {
    for(size_t i=0;i<haystack.len;i++) {
        if(haystack.data[i] == needle) {
            return haystack.data + i;
        }
    }

    return NULL;
}

int str_split(str *s, char delim, str *d) {

    if(s->len == 0) {
        return 0;
    }

    char *p = str_chr(*s, delim);
    if(p == NULL) {
        d->data = s->data;
        d->len  = s->len;

        s->len = 0;
    } else {
        d->data = s->data;
        d->len  = p - s->data;

        s->len  = s->len - d->len - 1;
        s->data = p + 1;
    }

    return 1;
}

#define da_append(xs, x) do{\
    if((xs)->len == (xs)->cap) {\
        if((xs)->cap == 0) (xs)->cap = 256;\
        else (xs)->cap <<= 1;\
        (xs)->data = realloc((xs)->data, (xs)->cap);\
    }\
    ((xs)->data)[(xs)->len++] = (x);\
}while(0)

#define da_append_many(xs, ys, ys_len) do{\
    size_t __FILE__##__LINE__##_cap = (xs)->cap;\
    if(__FILE__##__LINE__##_cap == 0) __FILE__##__LINE__##_cap = 256;\
    while(__FILE__##__LINE__##_cap < (xs)->len + (ys_len)) __FILE__##__LINE__##_cap <<= 1;\
    if(__FILE__##__LINE__##_cap != (xs)->cap) {\
        (xs)->cap = __FILE__##__LINE__##_cap;\
        (xs)->data = realloc((xs)->data, (xs)->cap);\
    }\
    memcpy((xs)->data + (xs)->len, (ys), (ys_len));\
    (xs)->len += (ys_len);\
}while(0)

typedef struct {
    char **data;
    size_t len;
    size_t cap;
} Cstrs;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} str_builder;

typedef struct {
    size_t off;
    size_t len;
    int is_mandatory;
} Command;

Cstrs cstrs = {0};
str_builder sb = {0};

#define COMMANDS_CAP 16
Command commands[COMMANDS_CAP] = {0};
size_t commands_len = 0;

int parse_command(str input) {

    if(input.len == 0) {
        return 0;
    }

    str part;
    while(str_split(&input, ' ', &part)) {
        da_append(&cstrs, (char *) sb.len);
        da_append_many(&sb, part.data, part.len);
        da_append(&sb, '\0');
    }    

    return 1;

}

void trim(str *s) {
    size_t i = 0;
    while(i < s->len && s->data[i] == ' ') i++;
    s->data += i;
    s->len  -= i;

    i = s->len;
    while(i > 0 && s->data[i - 1] == ' ') i--;
    s->len = i;
}

int parse_commands(str input) {

    size_t last = 0;
    for(size_t i=0;i<input.len;i++) {
        char c = input.data[i];

        if(c == '&') {
            if(last == i) {
                fprintf(stderr, "ERROR: Invalid input\n");
                return 0;
            }
            str command = { input.data + last, i - last };
            trim(&command);

            size_t off = cstrs.len;
            if(!parse_command(command)) {
                return 0;
            }
            size_t len = cstrs.len - off;

            int mandatory;
            if(i + 1 < input.len && input.data[i + 1] == '&') {
                i++;
                mandatory = 1;
            } else {
                mandatory = 0;
            }
            last = i + 1;

            if(COMMANDS_CAP <= commands_len) {
                fprintf(stderr, "ERROR: Exceeded maximum commands: %u\n", COMMANDS_CAP);
                return 0;
            }
            commands[commands_len++] = (Command) { off, len, mandatory };

        }

    }
    if(last != input.len) {
        str command = { input.data + last, input.len - last };
        trim(&command);

        size_t off = cstrs.len;
        if(!parse_command(command)) {
            return 0;
        }
        size_t len = cstrs.len - off;

        if(COMMANDS_CAP <= commands_len) {
            fprintf(stderr, "ERROR: Exceeded maximum commands: %u\n", COMMANDS_CAP);
            return 0;
        }
        commands[commands_len++] = (Command) { off, len, 0 };
    }

    for(size_t i=0;i<cstrs.len;i++) {
        cstrs.data[i] = sb.data + (size_t) cstrs.data[i];
    }

    return 1;
}

int execute_command(Command *c, int *exit_code) {

    printf("[CMD] '");
    for(size_t i=0;i<c->len;i++) {
        char *cstr = cstrs.data[c->off + i];
        printf("%s", cstr);
        if(i != c->len - 1) {
            printf(" ");
        }
    }
    printf("'\n");

    pid_t child_pid = fork();
    if(child_pid < 0) {
        fprintf(stderr, "[ERROR]: Failed to fork: '%s'\n", strerror(errno));
        return 0;
    }

    char *b = cstrs.data[c->off + c->len];
    cstrs.data[c->off + c->len] = NULL;

    if(child_pid == 0) {
        if(execvp(cstrs.data[c->off], cstrs.data + c->off) < 0) {
            fprintf(stderr, "[ERROR]: Could not execute command: '%s'\n", strerror(errno));
            return 0;
        }
    }

    for(;;) {
        int status;
        if(waitpid(child_pid, &status, 0) < 0) {
            fprintf(stderr, "[ERROR]: Could not wait for pid %d: '%s'\n", child_pid, strerror(errno));
            return 0;
        }

        if(WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
            break;
        }

        if(WIFSIGNALED(status)) {
            fprintf(stderr, "[ERROR]: Child process was terminated: '%s'\n", strsignal(WTERMSIG(status)));
            return 0;
        }
    }

    printf("[INFO] Exited with code: %d\n", *exit_code);

    cstrs.data[c->off + c->len] = b;

    return 1;
}

int execute_commands() {

    for(size_t i=0;i<commands_len;i++) {
        Command *c = &commands[i];

        int exit_code;
        if(!execute_command(c, &exit_code)) {
            return 0;
        }
        if(exit_code != 0 && c->is_mandatory) {
            break;
        }

    }
    return 1;

}

int main(int argc, char **argv) {

    if(argc < 3) {
        fprintf(stderr, "ERROR: Provide enough arguments\n");
        fprintf(stderr, "USAGE: %s <command> <filepath1> <filepath2> ...\n", argv[0]);
        return 1;
    }
    str input = { argv[1], strlen(argv[1]) };



    if(!parse_commands(input)) {
        return 1;
    }

    struct stat statbuf;    
    int first = 1;
    time_t filetime;
    for(int i=2;i<argc;i++) {
        char *filepath = argv[i];

        if(stat(filepath, &statbuf) < 0) {
            fprintf(stderr, "ERROR: Failed to query stats for '%s': %s\n", filepath, strerror(errno));
            return 1;
        }

        if(first || (statbuf.st_mtime > filetime)) {
            filetime = statbuf.st_mtime;
        }
        first = 0;
    }

    if(!execute_commands()) {
        return 1;
    }

    while(1) {

        first = 1;
        time_t challenger;
        for(int i=2;i<argc;i++) {
            char *filepath = argv[i];

            if(stat(filepath, &statbuf) < 0) {
                continue;
            }

            if(first || (statbuf.st_mtime > challenger)) {
                challenger = statbuf.st_mtime;
            }
            first = 0;
        }

        if(!first && (challenger > filetime)) {
            filetime = challenger;
            if(!execute_commands()) {
                return 1;
            }
        }

        usleep(500);

    }

    return 0;
}
