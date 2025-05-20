#include <stdio.h>

#include <windows.h>

int get_last_write(char *filepath, FILETIME *filetime) {

    HANDLE handle = CreateFileA(filepath,
    GENERIC_READ,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
    if(handle == INVALID_HANDLE_VALUE) {
	return 0;
    }

    if(!GetFileTime(
    handle,
    NULL,
    NULL,
    filetime)) {
	CloseHandle(handle);
	return 0;
    }

    CloseHandle(handle);

    return 1;
}

char *last_error_cstr(char *buf, size_t buf_len) {

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,
    NULL,
    GetLastError(),
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPSTR) buf,
    buf_len,
    NULL);

    size_t len = strlen(buf);
    if(buf[len - 1] == '\n') {
	buf[len - 1] = 0;
    }
    if(buf[len - 2] == '\r') {
	buf[len - 2] = 0;
    }

    return buf;
}

int execute(char *command, int *exit_code) {

    printf("[CMD] '%s'\n", command); fflush(stdout);

    STARTUPINFO si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if(!CreateProcess(NULL,
    command,
    NULL,
    NULL,
    FALSE,
    0,
    NULL,
    NULL,
    &si,
    &pi)) {
	char buf[1024];    
	fprintf(stderr, "[ERROR] Cannot start process '%s': '%s'",
	command,
	last_error_cstr(buf, sizeof(buf)));
	fflush(stderr);
	return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code;
    GetExitCodeProcess(pi.hProcess, &code);

    printf("[INFO] Exited with code: %ld\n", code); fflush(stdout);

    *exit_code = code;
    return 1;
}

typedef struct {
    char *data;
    size_t len;
} str;

typedef struct {
    str content;
    int is_mandatory;
} Command;

#define COMMANDS_CAP 16
static Command commands[COMMANDS_CAP] = {0};
static size_t commands_len = 0;

void trim(str *s) {
    size_t i = 0;
    while(i < s->len && s->data[i] == ' ') i++;
    s->data += i;
    s->len  -= i;

    i = s->len;
    while(i > 0 && s->data[i - 1] == ' ') i--;
    s->len = i;
}

int parse_command(str input) {

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
	    
	    int mandatory;
	    if(i + 1 < input.len && input.data[i + 1] == '&') {
		i++;
		mandatory = 1;
	    } else {
		mandatory = 0;
	    }
	    last = i + 1;

	    if(COMMANDS_CAP <= commands_len) {
		fprintf(stderr, "ERROR: Exceeded maximum commands: %llu\n", COMMANDS_CAP);
		return 0;
	    }
	    commands[commands_len++] = (Command) { command, mandatory };
	    
	}

    }
    if(last != input.len) {
	str command = { input.data + last, input.len - last };
	trim(&command);
	
	if(COMMANDS_CAP <= commands_len) {
	    fprintf(stderr, "ERROR: Exceeded maximum commands: %llu\n", COMMANDS_CAP);
	    return 0;
	}
	commands[commands_len++] = (Command) { command, 0 };
    }


    return 1;
}

int execute_commands() {

    for(size_t i=0;i<commands_len;i++) {
	Command *c = &commands[i];

	char b = c->content.data[c->content.len];
	c->content.data[c->content.len] = 0;

	int exit_code;
	if(!execute(c->content.data, &exit_code)) {
	    return 0;
	}
	if(exit_code != 0 && c->is_mandatory) {
	    break;
	}

	c->content.data[c->content.len] = b;
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
    
    if(!parse_command(input)) {
	return 1;
    }

    int first = 1;
    FILETIME filetime;
    for(int i=2;i<argc;i++) {
	char *filepath = argv[i];

	FILETIME temp;
	if(!get_last_write(filepath, &temp)) {
	    return 1;
	}

	if(first || (CompareFileTime(&filetime, &temp) < 0)) {
	    filetime = temp;
	}
	first = 0;
    }

    if(!execute_commands(input)) {
	return 1;
    }

    while(1) {

	first = 1;
	FILETIME challenger, temp;
	for(int i=2;i<argc;i++) {
	    char *filepath = argv[i];

	    if(!get_last_write(filepath, &temp)) {
		continue;
	    }

	    if(first || (CompareFileTime(&challenger, &temp) < 0)) {
		challenger = temp;
	    }
	    first = 0;
	}

	if(!first && (CompareFileTime(&filetime, &challenger) < 0)) {
	    filetime = challenger;
	    if(!execute_commands(input)) {
		return 1;
	    }
	}
	
	Sleep(10);

    }

    return 0;
}
