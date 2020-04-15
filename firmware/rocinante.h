#ifndef _ROCINANTE_H_
#define _ROCINANTE_H_

enum {
    COMMAND_CONTINUE = 0,
    COMMAND_FAILED,
    COMMAND_ADD_FAILED,
};

// Return COMMAND_CONTINUE to continue execution, return other to report an error and
// terminate operation of a script.
typedef int (*ProcessCommandFunc)(int wordcount, const char **words);

typedef struct Command {
    const char *name;   /* the command itself */
    int minWords;      /* including command, like argc */
    ProcessCommandFunc go;
    const char *form;   /* command form for usage message */
    const char *help;   /* human-readable guidance for command */
} Command;

int RegisterApp(const char* name, int minWords, ProcessCommandFunc go, const char *form, const char *help);

#endif /* _ROCINANTE_H_ */
