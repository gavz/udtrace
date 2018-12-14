#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "utils.h"

#define MAX_UNIX_FDS	32

#ifdef ENABLE_TITAN
extern void mncc_dissector(int fd, bool is_out, const char *fn, const uint8_t *data, unsigned int len);
extern void pcu_dissector(int fd, bool is_out, const char *fn, const uint8_t *data, unsigned int len);
#endif

/***********************************************************************
 * Utility functions
 ***********************************************************************/

/* taken from libosmocore */
static char hexd_buff[4096];
static const char hex_chars[] = "0123456789abcdef";
static char *hexdump(const unsigned char *buf, int len, char *delim)
{
	int i;
	char *cur = hexd_buff;

	hexd_buff[0] = 0;
	for (i = 0; i < len; i++) {
		const char *delimp = delim;
		int len_remain = sizeof(hexd_buff) - (cur - hexd_buff);
		if (len_remain < 3)
			break;

		*cur++ = hex_chars[buf[i] >> 4];
		*cur++ = hex_chars[buf[i] & 0xf];

		while (len_remain > 1 && *delimp) {
			*cur++ = *delimp++;
			len_remain--;
		}

		*cur = 0;
	}
	hexd_buff[sizeof(hexd_buff)-1] = 0;
	return hexd_buff;
}

static void default_dissector(int fd, bool is_out, const char *fn, const uint8_t *data, unsigned int len)
{
	fprintf(stderr, "%d %s %c %s\n", fd, fn, is_out ? 'W' : 'R', hexdump(data, len, ""));
}

static struct sock_state unix_fds[MAX_UNIX_FDS];

__attribute__ ((constructor)) static void udtrace_init(void)
{
	int i;
	LOG("Unix Domain Socket Trace initialized (TITAN support "
#ifdef ENABLE_TITAN
		"enabled"
#else
		"DISABLED"
#endif
			")\n");
	for (i = 0; i < ARRAY_SIZE(unix_fds); i++) {
		unix_fds[i] = (struct sock_state) { -1, NULL, NULL };
	}
}

/* add a file descriptor from the list of to-be-traced ones */
void udtrace_add_fd(int fd)
{
	struct sock_state *ss;

	/* Find an unused state in unix_fds */
	ss = udtrace_sstate_by_fd(-1);
	if (!ss) {
		LOG("Couldn't add UNIX FD %d (no space in unix_fds)\n", fd);
		return;
	}

	LOG("Adding FD %d\n", fd);
	ss->fd = fd;
}

/* add a file descriptor from the list of to-be-traced ones */
void udtrace_add_fd_child(int pfd, int cfd)
{
	struct sock_state *pss, *css;

	/* Find the parent socket state first */
	pss = udtrace_sstate_by_fd(pfd);
	if (!pss) {
		LOG("Couldn't find parent UNIX FD %d for %d\n", pfd, cfd);
		return;
	}

	/* Find an unused state in unix_fds */
	css = udtrace_sstate_by_fd(-1);
	if (!css) {
		LOG("Couldn't add UNIX FD %d (no space in unix_fds)\n", cfd);
		return;
	}

	LOG("Adding FD %d as a child of %d\n", cfd, pfd);

	css->dissector = pss->dissector;
	css->path = strdup(pss->path);
	css->fd = cfd;
}

/* delete a file descriptor from the list of to-be-traced ones */
void udtrace_del_fd(int fd)
{
	struct sock_state *ss;

	/* Find the corresponding state in unix_fds */
	ss = udtrace_sstate_by_fd(fd);
	if (!ss) {
		LOG("Couldn't delete UNIX FD %d (no such FD in unix_fds)\n", fd);
		return;
	}

	LOG("Removing FD %d\n", fd);
	free((void *) ss->path);
	*ss = (struct sock_state) { -1, NULL, NULL };
}

static void udtrace_resolve_dissector(struct sock_state *ss)
{
	/* actual useful dissectors resovled by path */
	if (0) { }
#ifdef ENABLE_TITAN
	else if (strstr(ss->path, "mncc"))
		ss->dissector = mncc_dissector;
	else if (strstr(ss->path, "pcu"))
		ss->dissector = pcu_dissector;
#endif
	else
		ss->dissector = &default_dissector;
}

/* set the path of a given fd */
void udtrace_fd_set_path(int fd, const char *path)
{
	struct sock_state *ss;

	/* Find the corresponding state in unix_fds */
	ss = udtrace_sstate_by_fd(fd);
	if (!ss) {
		LOG("Couldn't set path for UNIX FD %d (no such FD in unix_fds)\n", fd);
		return;
	}

	ss->path = strdup(path);
	udtrace_resolve_dissector(ss);
}

struct sock_state *udtrace_sstate_by_fd(int fd)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(unix_fds); i++) {
		if (unix_fds[i].fd == fd)
			return &unix_fds[i];
	}
	return NULL;
}

/* is the given file descriptor part of the to-be-traced unix domain fd's? */
bool is_unix_socket(int fd)
{
	if (udtrace_sstate_by_fd(fd))
		return true;
	else
		return false;
}

void udtrace_data(int fd, bool is_out, const char *fn, const uint8_t *data, unsigned int len)
{
	struct sock_state *ss = udtrace_sstate_by_fd(fd);
	if (!data || !len || !ss)
		return;
	ss->dissector(fd, is_out, fn, data, len);
}
