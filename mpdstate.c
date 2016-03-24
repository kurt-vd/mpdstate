/*
 * Copyright 2016 Kurt Van Dijck <dev.kurt@vandijck-laurijssen.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#define NAME "mpdstate"

/* non-daemon syslog */
static void mylog(int exitcode, int errnum, const char *fmt, ...)
{
	va_list va;
	static char str[1024];
	int ret;

	ret = sprintf(str, "%s: ", NAME);
	va_start(va, fmt);
	ret += vsprintf(str+ret, fmt, va);
	va_end(va);
	/* strip trailing newline */
	if (str[ret-1] == '\n')
		str[--ret] = 0;
	/* print error string */
	if (errnum)
		ret += sprintf(str+ret, ": %s\n", strerror(errnum));
	/* emit output */
	fputs(str, stderr);
	fputc('\n', stderr);
	fflush(stderr);
	if (exitcode)
		exit(exitcode);
}

static const char help_msg[] =
	NAME ": watch MPD state\n"
	"usage: " NAME " [OPTIONS ...] [CMD ARGS]\n"
	"	" NAME " [OPTIONS ...] -1 [PROPERTYNAME]\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -?, --help		Show this help message\n"
	" -h, --host=HOST	Connect to MPD on HOST\n"
	" -p, --port=PORT	MPD on PORT\n"
	" -1			Output all properties, or PROPERTYNAME, and exit\n"
	"\n"
	"Arguments\n"
	" When present, "NAME" executes CMD ARGS that receives\n"
	" the output of "NAME". Nothing is output on stdout\n"
	;

static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },

	{ "host", required_argument, NULL, 'h', },
	{ "port", required_argument, NULL, 'p', },
	{ },
};
static const char optstring[] = "+?Vh:p:1";

static int connect_uri(const char *host, int port, int preferred_type)
{
	int sock;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = preferred_type,
		.ai_protocol = 0,
		.ai_flags = 0,
	}, *paddr = NULL, *ai;
	char portstr[32];

	sprintf(portstr, "%u", port);
#ifdef AI_NUMERICSERV
	hints.ai_flags |= AI_NUMERICSERV;
#endif
	/* resolve host to IP */
	if (getaddrinfo(host, portstr, &hints, &paddr) < 0) {
		mylog(0, errno, "getaddrinfo %s %s", host, portstr);
		return -1;
	}
	/* create socket */
	for (ai = paddr; ai; ai = ai->ai_next) {
		sock = socket(ai->ai_family, ai->ai_socktype,
				ai->ai_protocol);
		if (sock < 0)
			continue;
		if (connect(sock, ai->ai_addr, ai->ai_addrlen) >= 0)
			/* success */
			break;
		close(sock);
	}
	freeaddrinfo(paddr);
	if (!ai)
		/* no more addrinfo left over */
		sock = -1;
	return sock;
}

static const char *const changes[] = {
	"player",
	"mixer",
	"options",
	"output",
	NULL,
};

static inline int send_recv(int sock, const char *msg, char *recvbuf, int recvbufsize)
{
	int ret;
	char *str;

	ret = asprintf(&str, "%s\n", msg);
	if (send(sock, str, ret, 0) < 0)
	if (ret < 0)
		mylog(1, errno, "send '%s'", msg);
	free(str);
	ret = recv(sock, recvbuf, recvbufsize, 0);
	if (ret < 0)
		mylog(1, errno, "recv '%s'", msg);
	return ret;
}

static char *propvalue(char *str)
{
	char *pos;

	pos = strstr(str, ": ");
	if (!pos)
		return NULL;
	*pos = 0;
	return pos+2;
}

static char buf[1024*16];

/* remember mpd state, in [x+0]=key, [x+1]=<value>, ... */
static char **state;
static int nstate, sstate; /* used vs. allocated */

static char **propcache(const char *propname)
{
	int j;

	for (j = 0; j < nstate; j += 2) {
		if (!strcmp(propname, state[j]))
			return state+j+1;
	}
	if ((nstate + 2) > sstate) {
		sstate += 128;
		state = realloc(state, sstate*sizeof(state[0]));
		if (!state)
			mylog(1, errno, "realloc state");
	}
	state[j] = strdup(propname);
	/* pre-assign a default value, and avoid multiple checks
	 * for the presence of a value
	 */
	state[j+1] = strdup("");
	nstate += 2;
	return state+j+1;
}

static int outputstate, outputsknown;


int main(int argc, char *argv[])
{
	int opt, sock, ret, changed, j, once = 0;
	const char *host = "localhost";
	int port = 6600;
	char *tok, *saved, *value;
	char *propname = NULL;

	/* parse program options */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s: %s\n", NAME, VERSION);
		return 0;
	default:
		fprintf(stderr, "%s: option '%c' unrecognised\n", NAME, opt);
	case '?':
		fputs(help_msg, stderr);
		exit(1);
		break;
	case 'h':
		host = optarg;
		break;
	case 'p':
		port = strtoul(optarg, NULL, 0);
		break;
	case '1':
		once = 1;
		break;
	}

	sock = connect_uri(host, port, SOCK_STREAM);
	if (sock < 0)
		mylog(1, 0, "could not connect to %s:%u", host, port);

	/* sync */
	ret = recv(sock, buf, sizeof(buf)-1, 0);
	if (ret < 0)
		mylog(1, errno, "recv");

	if (!once && argv[optind]) {
		/* fork child process */
		int pp[2];

		if (pipe(pp) < 0)
			mylog(1, errno, "pipe() failed\n");
		ret = fork();
		if (ret < 0)
			mylog(1, errno, "fork() failed\n");
		else if (!ret) {
			dup2(pp[0], STDIN_FILENO);
			close(pp[0]);
			close(pp[1]);
			close(sock);
			execvp(argv[optind], argv+optind);
			mylog(1, errno, "execvp %s ...", argv[optind]);
		}
		dup2(pp[1], STDOUT_FILENO);
		close(pp[0]);
		close(pp[1]);
	} else if (once && argv[optind]) {
		/* fetch only 1 property */
		propname = argv[optind++];
	}

	/* ask everything */
	changed = ~0;
	while (1) {
		if (changed & 7) {
			char **pcache;
			/* grab status */
			ret = send_recv(sock, "status", buf, sizeof(buf)-1);
			buf[ret] = 0;
			for (tok = strtok_r(buf, "\n\r", &saved); tok; tok = strtok_r(NULL, "\n\r", &saved)) {
				if (!strcmp(tok, "OK"))
					break;
				value = propvalue(tok);
				if (!value)
					continue;
				pcache = propcache(tok);
				if (strcmp(value, *pcache)) {
					free(*pcache);
					*pcache = strdup(value);
					if (propname && !strcmp(propname, tok)) {
						printf("%s\n", value);
						exit(0);
					} else if (!propname)
						printf("%s\t%s\n", tok, value);
				}
			}
		}
		if (changed & (1 << 3)) {
			/* grab outputs */
			int id;
			char *name;

			ret = send_recv(sock, "outputs", buf, sizeof(buf)-1);
			buf[ret] = 0;
			for (tok = strtok_r(buf, "\n\r", &saved); tok; tok = strtok_r(NULL, "\n\r", &saved)) {
				if (!strcmp(tok, "OK"))
					break;
				value = propvalue(tok);
				if (!strcmp(tok, "outputid"))
					id = strtoul(value, NULL, 0);
				else if (!strcmp(tok, "outputname"))
					name = value;
				else if (!strcmp(tok, "outputenabled")) {
					int mask = 1 << id;
					int state = !!strtoul(value, NULL, 0) << id;

					if (propname && !strncmp("output", propname, 6) && (strtoul(propname+6, NULL, 0) == id)) {
						printf("%s\n", value);
						exit(0);
					} else if (!propname && (~outputsknown | (outputstate ^ state)) & mask) {
						/* new output or different state */
						outputsknown |= mask;
						outputstate = (outputstate & ~mask) | state;
						printf("output%i:\"%s\"\t%s\n", id, name, value);
					}
				}
			}
		}
		fflush(stdout);
		if (once)
			break;

		/* wait for events */
		ret = send_recv(sock, "idle", buf, sizeof(buf)-1);
		buf[ret] = 0;
		changed = 0;
		for (tok = strtok_r(buf, "\n\r", &saved); tok; tok = strtok_r(NULL, "\n\r", &saved)) {
			if (!strcmp(tok, "OK"))
				break;
			for (tok = strtok(tok, ": "); tok; tok = strtok(NULL, ";, ")) {
				for (j = 0; changes[j]; ++j) {
					if (!strcmp(tok, changes[j])) {
						changed |= 1 << j;
						break;
					}
				}
			}
		}
	}
	return 0;
}
