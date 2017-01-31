#include "s.h"
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

char *s_dup(const char *str)
{
	char *dup;
	if(!str) return NULL;
	dup = malloc(strlen(str) + 1);
	strcpy(dup, str);
	return dup;
}

char *s_ndup(const char *str, size_t n)
{
	size_t len;
	char *dup;
	if(!str) return NULL;
	len = strlen(str);
	if(n < len) len = n;
	dup = malloc(len + 1);
	memcpy(dup, str, len);
	dup[len] = '\0';
	return dup;
}

char *s_concat(const char *s1, ...)
{
	va_list args;
	const char *i;
	size_t len;
	char *out;
	len = 0;
	va_start(args, s1);
	for(i = s1; i; i = va_arg(args, const char *)) len += strlen(i);
	va_end(args);
	out = malloc(len + 1);
	if(!out) return NULL;
	out[0] = '\0';
	va_start(args, s1);
	for(i = s1; i; i = va_arg(args, const char *)) strcat(out, i);
	va_end(args);
	return out;
}

char *s_getline(void *file)
{
	size_t sz = 128, i = 0;
	char *buf = NULL;
	while(1) {
		int c;
		c = getc(file);
		if(c == EOF && i == 0) break;
		if(c == EOF || c == '\n') c = '\0';

		if(!buf) buf = malloc(sz);
		else if(i == sz - 1) {
			char *newbuf;
			newbuf = realloc(buf, sz *= 2);
			if(!newbuf) free(buf);
			buf = newbuf;
		}
		if(!buf) return NULL;

		buf[i] = c;
		if(!c) break;
		++i;
	}
	return buf;
}

char *s_getstr(int fd)
{
	size_t sz = 128, i = 0;
	char *buf = NULL;
	while(1) {
		char c;
		if(read(fd, &c, 1) < 1) c = '\0';

		if(!buf) buf = malloc(sz);
		else if(i == sz - 1) {
			char *newbuf;
			newbuf = realloc(buf, sz *= 2);
			if(!newbuf) free(buf);
			buf = newbuf;
		}
		if(!buf) return NULL;

		buf[i] = c;
		if(!c) break;
		++i;
	}
	return buf;
}
