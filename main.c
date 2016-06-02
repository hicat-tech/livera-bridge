/*
 * Websocket to serial bridge
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include "libwebsockets.h"

#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>


#ifdef WEBSOCKET_TO_SERIAL
#include "libserial.h"

static volatile int serial_fd = -1;
#endif

static volatile int force_exit = 0;
static int versa, state;
static int times = -1;

#define MAX_ECHO_PAYLOAD 1024

struct per_session_data__echo {
	size_t rx, tx;
	unsigned char buf[LWS_PRE + MAX_ECHO_PAYLOAD];
	unsigned int len;
	unsigned int index;
	int final;
	int continuation;
	int binary;
};

static int
callback_echo(struct lws *wsi, enum lws_callback_reasons reason, void *user,
	      void *in, size_t len)
{
	struct per_session_data__echo *pss =
			(struct per_session_data__echo *)user;
	int n;

	switch (reason) {
	case LWS_CALLBACK_SERVER_WRITEABLE:
		break;
	case LWS_CALLBACK_RECEIVE:
		lwsl_info("rx:[%s]", (char *)in);

#ifdef WEBSOCKET_TO_SERIAL
		if (serial_fd < 0) {
			serial_fd = UART_Open("/dev/ttyAMA1");
			if (serial_fd < 0) {
				printf("Failed to open /dev/ttyAMA1\n");
			} else {
				printf("/dev/ttyAMA1 is opened");
			}
			UART_Init(serial_fd, 115200);
		}

		UART_Send(serial_fd, in, len);
#endif

		break;
	case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
		/* reject everything else except permessage-deflate */
		if (strcmp(in, "permessage-deflate"))
			return 1;
		break;

	default:
		break;
	}

	return 0;
}



static struct lws_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		"",		/* name - can be overriden with -e */
		callback_echo,
		sizeof(struct per_session_data__echo),	/* per_session_data_size */
		MAX_ECHO_PAYLOAD,
	},
	{
		NULL, NULL, 0		/* End of list */
	}
};


void sighandler(int sig)
{
	force_exit = 1;
}

static struct option options[] = {
	{ "help",	no_argument,		NULL, 'h' },
	{ "debug",	required_argument,	NULL, 'd' },
	{ "port",	required_argument,	NULL, 'p' },
	{ "versa",	no_argument,		NULL, 'v' },
	{ "uri",	required_argument,	NULL, 'u' },
	{ "passphrase", required_argument,	NULL, 'P' },
	{ "interface",  required_argument,	NULL, 'i' },
	{ "times",	required_argument,	NULL, 'n' },
	{ "echogen",	no_argument,		NULL, 'e' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", 	no_argument,		NULL, 'D' },
#endif
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	int n = 0;
	int port = 7681;
	int use_ssl = 0;
	struct lws_context *context;
	int opts = 0;
	char interface_name[128] = "";
	const char *_interface = NULL;
	int syslog_options = LOG_PID | LOG_PERROR;
	int client = 0;
	int listen_port = 80;
	struct lws_context_creation_info info;
	char passphrase[256];
	char uri[256] = "/";

	int debug_level = 15;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	memset(&info, 0, sizeof info);

	lwsl_notice("Built to support server operations\n");

	while (n >= 0) {
		n = getopt_long(argc, argv, "i:hp:d:Dk:P:vu:n:e"
				, options, NULL);
		if (n < 0)
			continue;
		switch (n) {
		case 'P':
			strncpy(passphrase, optarg, sizeof(passphrase));
			passphrase[sizeof(passphrase) - 1] = '\0';
			info.ssl_private_key_password = passphrase;
			break;
		case 'u':
			strncpy(uri, optarg, sizeof(uri));
			uri[sizeof(uri) - 1] = '\0';
			break;

#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			syslog_options &= ~LOG_PERROR;
			break;
#endif
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'v':
			versa = 1;
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof interface_name);
			interface_name[(sizeof interface_name) - 1] = '\0';
			_interface = interface_name;
			break;
		case 'n':
			times = atoi(optarg);
			break;
		case '?':
		case 'h':
			fprintf(stderr, "Usage: libwebsockets-test-echo\n"
				"  --debug      / -d <debug bitfield>\n"
				"  --port       / -p <port>\n"
				"  --passphrase / -P <passphrase>\n"
				"  --interface  / -i <interface>\n"
				"  --uri        / -u <uri path>\n"
				"  --times      / -n <-1 unlimited or times to echo>\n"
#ifndef LWS_NO_DAEMONIZE
				"  --daemonize  / -D\n"
#endif
			);
			exit(1);
		}
	}

#ifndef LWS_NO_DAEMONIZE
	/*
	 * normally lock path would be /var/lock/lwsts or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.lwsts-lock
	 */
	if (!client && daemonize && lws_daemonize("/tmp/.lwstecho-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 1;
	}
#endif

#ifdef WEBSOCKET_TO_SERIAL
	if (serial_fd < 0) {
		serial_fd = UART_Open("/dev/ttyAMA1");
		if (serial_fd < 0) {
			printf("Failed to open /dev/ttyAMA1\n");
		} else {
			printf("/dev/ttyAMA1 is opened");
		}
		UART_Init(serial_fd, 115200);
	}
#endif

	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("lwsts", syslog_options, LOG_DAEMON);

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);

	lwsl_notice("libwebsockets test server echo - license LGPL2.1+SLE\n");
	lwsl_notice("(C) Copyright 2010-2016 Andy Green <andy@warmcat.com>\n");

	lwsl_notice("Running in server mode\n");
	listen_port = port;

	info.port = listen_port;
	info.iface = _interface;
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	info.options = opts | LWS_SERVER_OPTION_VALIDATE_UTF8;

	context = lws_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}

	signal(SIGINT, sighandler);

	n = 0;
	while (n >= 0 && !force_exit) {
		n = lws_service(context, 10);
	}

	lws_context_destroy(context);

	lwsl_notice("libwebsockets-test-echo exited cleanly\n");
	closelog();

#ifdef WEBSOCKET_TO_SERIAL
	UART_Close(serial_fd);
#endif

	return 0;
}
