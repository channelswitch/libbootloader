#include <string.h>
/* Like strdup and strndup. Return NULL if str == NULL. */
char *s_dup(const char *str);
char *s_ndup(const char *str, size_t n);

char *s_concat(const char *s1, ...);

/* Read a line from a file (FILE *). Returns a newly-allocagted string. Neither
 * newlines nor the empty line after the newline at the end of the file are
 * included in the output. '\0' also terminates the line. */
char *s_getline(void *file);

/* Read a null-terminated string from an fd. */
char *s_getstr(int fd);
