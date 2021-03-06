/* pop3d.c -- POP3 server protocol parsing
 *
 * Copyright (c) 1998-2003 Carnegie Mellon University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any other legal
 *    details, please contact  
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * $Id: pop3d.c,v 1.171 2007/02/05 18:41:48 jeaton Exp $
 */
#include <config.h>


#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/param.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "prot.h"

#include <sasl/sasl.h>
#include <sasl/saslutil.h>

#include "acl.h"
#include "util.h"
#include "auth.h"
#include "iptostring.h"
#include "global.h"
#include "tls.h"

#include "exitcodes.h"
#include "imap_err.h"
#include "mailbox.h"
#include "version.h"
#include "xmalloc.h"
#include "xstrlcpy.h"
#include "xstrlcat.h"
#include "mboxlist.h"
#include "idle.h"
#include "telemetry.h"
#include "backend.h"
#include "proxy.h"

#include "sync_log.h"

#ifdef APPLE_OS_X_SERVER
#include "AppleOD.h"

enum {
    STATUS_FD = 3,
    LISTEN_FD = 4
};

enum {
    MASTER_SERVICE_AVAILABLE = 0x01,
    MASTER_SERVICE_UNAVAILABLE = 0x02,
    MASTER_SERVICE_CONNECTION = 0x03,
    MASTER_SERVICE_CONNECTION_MULTI = 0x04,
    MASTER_SERVICE_STATUS_ADD_USER = 0x05,
    MASTER_SERVICE_STATUS_REMOVE_USER = 0x06
};

struct notify_message {
    int message;
    pid_t service_pid;
	time_t	p_start_time;
	char p_user_buf[64+1];
	char p_host_buf[64+1];
};

#endif

#ifdef HAVE_KRB
/* kerberos des is purported to conflict with OpenSSL DES */
#define DES_DEFS
#include <krb.h>

/* MIT's kpop authentication kludge */
char klrealm[REALM_SZ];
AUTH_DAT kdata;
#endif /* HAVE_KRB */
static int kflag = 0;

extern int optind;
extern char *optarg;
extern int opterr;



#ifdef HAVE_SSL
static SSL *tls_conn;
#endif /* HAVE_SSL */

sasl_conn_t *popd_saslconn; /* the sasl connection context */

char *popd_userid = 0, *popd_subfolder = 0;
struct mailbox *popd_mailbox = 0;
struct auth_state *popd_authstate = 0;
int config_popuseacl;
struct sockaddr_storage popd_localaddr, popd_remoteaddr;
int popd_haveaddr = 0;
char popd_clienthost[NI_MAXHOST*2+1] = "[local]";
struct protstream *popd_out = NULL;
struct protstream *popd_in = NULL;
static int popd_logfd = -1;
unsigned popd_exists = 0;
unsigned popd_login_time;
struct msg {
    unsigned uid;
    unsigned size;
    int deleted;
} *popd_msg = NULL;

static sasl_ssf_t extprops_ssf = 0;
static int pop3s = 0;
int popd_starttls_done = 0;

static struct mailbox mboxstruct;

static mailbox_decideproc_t expungedeleted;

/* the sasl proxy policy context */
static struct proxy_context popd_proxyctx = {
    0, 1, &popd_authstate, NULL, NULL
};

/* signal to config.c */
const int config_need_data = CONFIG_NEED_PARTITION_DATA;

/* current namespace */
static struct namespace popd_namespace;

#ifdef APPLE_OS_X_SERVER
/* current user mail options */
static struct od_user_opts	*gUserOpts = NULL;
#endif

/* PROXY stuff */
struct backend *backend = NULL;

static void bitpipe(void);
/* end PROXY stuff */

static char popd_apop_chal[45 + MAXHOSTNAMELEN + 1]; /* <rand.time@hostname> */
static void cmd_apop(char *response);

static void cmd_auth(char *arg);
static void cmd_capa(void);
static void cmd_pass(char *pass);
static void cmd_user(char *user);
static void cmd_starttls(int pop3s);
static void blat(int msg,int lines);
static int openinbox(void);
static void cmdloop(void);
static void kpop(void);
static int parsenum(char **ptr);
void usage(void);
void shut_down(int code) __attribute__ ((noreturn));


extern void setproctitle_init(int argc, char **argv, char **envp);
extern int proc_register(const char *progname, const char *clienthost, 
			 const char *userid, const char *mailbox);
extern void proc_cleanup(void);

extern int saslserver(sasl_conn_t *conn, const char *mech,
		      const char *init_resp, const char *resp_prefix,
		      const char *continuation, const char *empty_chal,
		      struct protstream *pin, struct protstream *pout,
		      int *sasl_result, char **success_data);

/* Enable the resetting of a sasl_conn_t */
static int reset_saslconn(sasl_conn_t **conn);

static struct 
{
    char *ipremoteport;
    char *iplocalport;
    sasl_ssf_t ssf;
    char *authid;
} saslprops = {NULL,NULL,0,NULL};

static int popd_canon_user(sasl_conn_t *conn, void *context,
			   const char *user, unsigned ulen,
			   unsigned flags, const char *user_realm,
			   char *out, unsigned out_max, unsigned *out_ulen)
{
    char userbuf[MAX_MAILBOX_NAME+1], *p;
    size_t n;
    int r;

    if (!ulen) ulen = strlen(user);

    if (config_getswitch(IMAPOPT_POPSUBFOLDERS)) {
	/* make a working copy of the auth[z]id */
	if (ulen > MAX_MAILBOX_NAME) {
	    sasl_seterror(conn, 0, "buffer overflow while canonicalizing");
	    return SASL_BUFOVER;
	}
	memcpy(userbuf, user, ulen);
	userbuf[ulen] = '\0';
	user = userbuf;

	/* See if we're trying to access a subfolder */
	if ((p = strchr(userbuf, '+'))) {
	    n = config_virtdomains ? strcspn(p, "@") : strlen(p);

	    if (flags & SASL_CU_AUTHZID) {
		/* make a copy of the subfolder */
		if (popd_subfolder) free(popd_subfolder);
		popd_subfolder = xstrndup(p, n);
	    }

	    /* strip the subfolder from the auth[z]id */
	    memmove(p, p+n, strlen(p+n)+1);
	    ulen -= n;
	}
    }

    r = mysasl_canon_user(conn, context, user, ulen, flags, user_realm,
			  out, out_max, out_ulen);

    if (!r && popd_subfolder && flags == SASL_CU_AUTHZID) {
	/* If we're only doing the authzid, put back the subfolder
	   in case its used in the challenge/response calculation */
	n = strlen(popd_subfolder);
	if (*out_ulen + n > out_max) {
	    sasl_seterror(conn, 0, "buffer overflow while canonicalizing");
	    r = SASL_BUFOVER;
	}
	else {
	    p = (config_virtdomains && (p = strchr(out, '@'))) ?
		p : out + *out_ulen;
	    memmove(p+n, p, strlen(p)+1);
	    memcpy(p, popd_subfolder, n);
	    *out_ulen += n;
	}
    }

    return r;
}

static int popd_proxy_policy(sasl_conn_t *conn,
			     void *context,
			     const char *requested_user, unsigned rlen,
			     const char *auth_identity, unsigned alen,
			     const char *def_realm,
			     unsigned urlen,
			     struct propctx *propctx)
{
    if (config_getswitch(IMAPOPT_POPSUBFOLDERS)) {
	char userbuf[MAX_MAILBOX_NAME+1], *p;
	size_t n;

	/* make a working copy of the authzid */
	if (!rlen) rlen = strlen(requested_user);
	if (rlen > MAX_MAILBOX_NAME) {
	    sasl_seterror(conn, 0, "buffer overflow while proxying");
	    return SASL_BUFOVER;
	}
	memcpy(userbuf, requested_user, rlen);
	userbuf[rlen] = '\0';
	requested_user = userbuf;

	/* See if we're trying to access a subfolder */
	if ((p = strchr(userbuf, '+'))) {
	    n = config_virtdomains ? strcspn(p, "@") : strlen(p);

	    /* strip the subfolder from the authzid */
	    memmove(p, p+n, strlen(p+n)+1);
	    rlen -= n;
	}
    }

    return mysasl_proxy_policy(conn, context, requested_user, rlen,
			       auth_identity, alen, def_realm, urlen, propctx);
}

static struct sasl_callback mysasl_cb[] = {
    { SASL_CB_GETOPT, &mysasl_config, NULL },
    { SASL_CB_PROXY_POLICY, &popd_proxy_policy, (void*) &popd_proxyctx },
    { SASL_CB_CANON_USER, &popd_canon_user, NULL },
    { SASL_CB_LIST_END, NULL, NULL }
};

static void popd_reset(void)
{
    proc_cleanup();

    /* close local mailbox */
    if (popd_mailbox) {
	mailbox_close(popd_mailbox);
	popd_mailbox = 0;
    }

    /* close backend connection */
    if (backend) {
	backend_disconnect(backend);
	free(backend);
	backend = NULL;
    }

    if (popd_in) {
	prot_NONBLOCK(popd_in);
	prot_fill(popd_in);
	
	prot_free(popd_in);
    }

    if (popd_out) {
	prot_flush(popd_out);
	prot_free(popd_out);
    }
    
    popd_in = popd_out = NULL;

#ifdef HAVE_SSL
    if (tls_conn) {
	tls_reset_servertls(&tls_conn);
	tls_conn = NULL;
    }
#endif

    cyrus_reset_stdio();

    strcpy(popd_clienthost, "[local]");
    if (popd_logfd != -1) {
	close(popd_logfd);
	popd_logfd = -1;
    }
    if (popd_userid != NULL) {
	free(popd_userid);
	popd_userid = NULL;
    }
    if (popd_subfolder != NULL) {
	free(popd_subfolder);
	popd_subfolder = NULL;
    }
    if (popd_authstate) {
	auth_freestate(popd_authstate);
	popd_authstate = NULL;
    }
    if (popd_saslconn) {
	sasl_dispose(&popd_saslconn);
	popd_saslconn = NULL;
    }
    popd_starttls_done = 0;

    if(saslprops.iplocalport) {
       free(saslprops.iplocalport);
       saslprops.iplocalport = NULL;
    }
    if(saslprops.ipremoteport) {
       free(saslprops.ipremoteport);
       saslprops.ipremoteport = NULL;
    }
    if(saslprops.authid) {
       free(saslprops.authid);
       saslprops.authid = NULL;
    }
    saslprops.ssf = 0;

    popd_exists = 0;
}

/*
 * run once when process is forked;
 * MUST NOT exit directly; must return with non-zero error code
 */
int service_init(int argc __attribute__((unused)),
		 char **argv __attribute__((unused)),
		 char **envp __attribute__((unused)))
{
    int r;
    int opt;

    if (geteuid() == 0) fatal("must run as the Cyrus user", EC_USAGE);
    setproctitle_init(argc, argv, envp);

    /* set signal handlers */
    signals_set_shutdown(&shut_down);
    signal(SIGPIPE, SIG_IGN);

    /* load the SASL plugins */
    global_sasl_init(1, 1, mysasl_cb);

    /* open the mboxlist, we'll need it for real work */
    mboxlist_init(0);
    mboxlist_open(NULL);

    /* open the quota db, we'll need it for expunge */
    quotadb_init(0);
    quotadb_open(NULL);

    /* setup for sending IMAP IDLE notifications */
    idle_enabled();

    /* Set namespace */
    if ((r = mboxname_init_namespace(&popd_namespace, 1)) != 0) {
	syslog(LOG_ERR, error_message(r));
	fatal(error_message(r), EC_CONFIG);
    }

    while ((opt = getopt(argc, argv, "skp:")) != EOF) {
	switch(opt) {
	case 's': /* pop3s (do starttls right away) */
	    pop3s = 1;
	    if (!tls_enabled()) {
		syslog(LOG_ERR, "pop3s: required OpenSSL options not present");
		fatal("pop3s: required OpenSSL options not present",
		      EC_CONFIG);
	    }
	    break;

	case 'k':
	    kflag++;
	    break;

	case 'p': /* external protection */
	    extprops_ssf = atoi(optarg);
	    break;

	default:
	    usage();
	}
    }

    return 0;
}

/*
 * run for each accepted connection
 */
int service_main(int argc __attribute__((unused)),
		 char **argv __attribute__((unused)),
		 char **envp __attribute__((unused)))
{
    socklen_t salen;
    char hbuf[NI_MAXHOST];
    char localip[60], remoteip[60];
    int niflags;
    int timeout;
    sasl_security_properties_t *secprops=NULL;

    signals_poll();

    sync_log_init();

    popd_in = prot_new(0, 0);
    popd_out = prot_new(1, 1);

#ifdef APPLE_OS_X_SERVER
	if (gUserOpts == NULL){
	gUserOpts = xzmalloc(sizeof(struct od_user_opts));
	}
#endif

    /* Find out name of client host */
    salen = sizeof(popd_remoteaddr);
    if (getpeername(0, (struct sockaddr *)&popd_remoteaddr, &salen) == 0 &&
	(popd_remoteaddr.ss_family == AF_INET ||
	 popd_remoteaddr.ss_family == AF_INET6)) {
	if (getnameinfo((struct sockaddr *)&popd_remoteaddr, salen,
			hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD) == 0) {
    	    strncpy(popd_clienthost, hbuf, sizeof(hbuf));
	    strlcat(popd_clienthost, " ", sizeof(popd_clienthost));
	} else {
	    popd_clienthost[0] = '\0';
	}
	niflags = NI_NUMERICHOST;
#ifdef NI_WITHSCOPEID
	if (((struct sockaddr *)&popd_remoteaddr)->sa_family == AF_INET6)
	    niflags |= NI_WITHSCOPEID;
#endif
	if (getnameinfo((struct sockaddr *)&popd_remoteaddr, salen, hbuf,
			sizeof(hbuf), NULL, 0, niflags) != 0)
	    strlcpy(hbuf, "unknown", sizeof(hbuf));
	strlcat(popd_clienthost, "[", sizeof(popd_clienthost));
	strlcat(popd_clienthost, hbuf, sizeof(popd_clienthost));
	strlcat(popd_clienthost, "]", sizeof(popd_clienthost));
	salen = sizeof(popd_localaddr);
	if (getsockname(0, (struct sockaddr *)&popd_localaddr, &salen) == 0) {
	    popd_haveaddr = 1;
	}
    }

    /* other params should be filled in */
    if (sasl_server_new("pop", config_servername, NULL, NULL, NULL,
			NULL, 0, &popd_saslconn) != SASL_OK)
	fatal("SASL failed initializing: sasl_server_new()",EC_TEMPFAIL); 

    /* will always return something valid */
    secprops = mysasl_secprops(SASL_SEC_NOPLAINTEXT);
    sasl_setprop(popd_saslconn, SASL_SEC_PROPS, secprops);
    sasl_setprop(popd_saslconn, SASL_SSF_EXTERNAL, &extprops_ssf);
    
    if(iptostring((struct sockaddr *)&popd_localaddr,
		  salen, localip, 60) == 0) {
	sasl_setprop(popd_saslconn, SASL_IPLOCALPORT, localip);
	saslprops.iplocalport = xstrdup(localip);
    }
    
    if(iptostring((struct sockaddr *)&popd_remoteaddr,
		  salen, remoteip, 60) == 0) {
	sasl_setprop(popd_saslconn, SASL_IPREMOTEPORT, remoteip);  
	saslprops.ipremoteport = xstrdup(remoteip);
    }

    proc_register("pop3d", popd_clienthost, NULL, NULL);

    /* Set inactivity timer */
    timeout = config_getint(IMAPOPT_POPTIMEOUT);
    if (timeout < 10) timeout = 10;
    prot_settimeout(popd_in, timeout*60);
    prot_setflushonread(popd_in, popd_out);

    if (kflag) kpop();

    /* we were connected on pop3s port so we should do 
       TLS negotiation immediatly */
    if (pop3s == 1) cmd_starttls(1);

    /* Create APOP challenge for banner */
    *popd_apop_chal = 0;
    if (config_getswitch(IMAPOPT_ALLOWAPOP) &&
	(sasl_checkapop(popd_saslconn, NULL, 0, NULL, 0) == SASL_OK) &&
	!sasl_mkchal(popd_saslconn,
		     popd_apop_chal, sizeof(popd_apop_chal), 1)) {
	syslog(LOG_WARNING, "APOP disabled: can't create challenge");
    }

    prot_printf(popd_out, "+OK %s Cyrus POP3%s %s server ready %s\r\n",
		config_servername, config_mupdate_server ? " Murder" : "",
		CYRUS_VERSION, popd_apop_chal);
    cmdloop();

    /* QUIT executed */

    /* don't bother reusing KPOP connections */
    if (kflag) shut_down(0);

    /* cleanup */
    popd_reset();

    return 0;
}

/* Called by service API to shut down the service */
void service_abort(int error)
{
    shut_down(error);
}

void usage(void)
{
    prot_printf(popd_out, "-ERR usage: pop3d [-C <alt_config>] [-k] [-s]\r\n");
    prot_flush(popd_out);
    exit(EC_USAGE);
}

/*
 * Cleanly shut down and exit
 */
void shut_down(int code)
{
    proc_cleanup();

    /* close local mailbox */
    if (popd_mailbox) {
	mailbox_close(popd_mailbox);
    }

    if (popd_msg) {
	free(popd_msg);
    }

    /* close backend connection */
    if (backend) {
	backend_disconnect(backend);
	free(backend);
    }

#ifdef APPLE_OS_X_SERVER
	if (gUserOpts) {
	odFreeUserOpts(gUserOpts, 1);
	free(gUserOpts);
	gUserOpts = NULL;
	}
#endif

    mboxlist_close();
    mboxlist_done();

    quotadb_close();
    quotadb_done();

    if (popd_in) {
	prot_NONBLOCK(popd_in);
	prot_fill(popd_in);
	prot_free(popd_in);
    }

    if (popd_out) {
	prot_flush(popd_out);
	prot_free(popd_out);
    }

#ifdef HAVE_SSL
    tls_shutdown_serverengine();
#endif

    cyrus_done();

    exit(code);
}

void fatal(const char* s, int code)
{
    static int recurse_code = 0;

    if (recurse_code) {
	/* We were called recursively. Just give up */
	proc_cleanup();
	exit(recurse_code);
    }
    recurse_code = code;
    if (popd_out) {
	prot_printf(popd_out, "-ERR [SYS/PERM] Fatal error: %s\r\n", s);
	prot_flush(popd_out);
    }
    syslog(LOG_ERR, "Fatal error: %s", s);
    shut_down(code);
}

#ifdef HAVE_KRB
/* translate IPv4 mapped IPv6 address to IPv4 address */
#ifdef IN6_IS_ADDR_V4MAPPED
static void sockaddr_unmapped(struct sockaddr *sa, socklen_t *len)
{
    struct sockaddr_in6 *sin6;
    struct sockaddr_in *sin4;
    uint32_t addr;
    int port;

    if (sa->sa_family != AF_INET6)
	return;
    sin6 = (struct sockaddr_in6 *)sa;
    if (!IN6_IS_ADDR_V4MAPPED((&sin6->sin6_addr)))
	return;
    sin4 = (struct sockaddr_in *)sa;
    addr = *(uint32_t *)&sin6->sin6_addr.s6_addr[12];
    port = sin6->sin6_port;
    memset(sin4, 0, sizeof(struct sockaddr_in));
    sin4->sin_addr.s_addr = addr;
    sin4->sin_port = port;
    sin4->sin_family = AF_INET;
#ifdef HAVE_SOCKADDR_SA_LEN
    sin4->sin_len = sizeof(struct sockaddr_in);
#endif
    *len = sizeof(struct sockaddr_in);
}
#else
static void sockaddr_unmapped(struct sockaddr *sa __attribute__((unused)),
			      socklen_t *len __attribute__((unused)))
{
    return;
}
#endif


/*
 * MIT's kludge of a kpop protocol
 * Client does a krb_sendauth() first thing
 */
void kpop(void)
{
    Key_schedule schedule;
    KTEXT_ST ticket;
    char instance[INST_SZ];  
    char version[9];
    const char *srvtab;
    int r;
    socklen_t len;
    
    if (!popd_haveaddr) {
	fatal("Cannot get client's IP address", EC_OSERR);
    }

    srvtab = config_getstring(IMAPOPT_SRVTAB);

    sockaddr_unmapped((struct sockaddr *)&popd_remoteaddr, &len);
    if (popd_remoteaddr.ss_family != AF_INET) {
	prot_printf(popd_out,
		    "-ERR [AUTH] Kerberos authentication failure: %s\r\n",
		    "not an IPv4 connection");
	shut_down(0);
    }

    strcpy(instance, "*");
    r = krb_recvauth(0L, 0, &ticket, "pop", instance,
		     (struct sockaddr_in *) &popd_remoteaddr,
		     (struct sockaddr_in *) NULL,
		     &kdata, (char*) srvtab, schedule, version);
    
    if (r) {
	prot_printf(popd_out, "-ERR [AUTH] Kerberos authentication failure: %s\r\n",
		    krb_err_txt[r]);
	syslog(LOG_NOTICE,
	       "badlogin: %s kpop ? %s%s%s@%s %s",
	       popd_clienthost, kdata.pname,
	       kdata.pinst[0] ? "." : "", kdata.pinst,
	       kdata.prealm, krb_err_txt[r]);
	shut_down(0);
    }
    
    r = krb_get_lrealm(klrealm,1);
    if (r) {
	prot_printf(popd_out, "-ERR [AUTH] Kerberos failure: %s\r\n",
		    krb_err_txt[r]);
	syslog(LOG_NOTICE,
	       "badlogin: %s kpop ? %s%s%s@%s krb_get_lrealm: %s",
	       popd_clienthost, kdata.pname,
	       kdata.pinst[0] ? "." : "", kdata.pinst,
	       kdata.prealm, krb_err_txt[r]);
	shut_down(0);
    }
}
#else
void kpop(void)
{
    usage();
}
#endif

/*
 * Top-level command loop parsing
 */
static void cmdloop(void)
{
    char inputbuf[8192];
    char *p, *arg;
    unsigned msg = 0;

    for (;;) {
	signals_poll();

	if (backend) {
	    /* create a pipe from client to backend */
	    bitpipe();

	    /* pipe has been closed */
	    return;
	}

	/* check for shutdown file */
	if (shutdown_file(inputbuf, sizeof(inputbuf))) {
	    for (p = inputbuf; *p == '['; p++); /* can't have [ be first char */
	    prot_printf(popd_out, "-ERR [SYS/TEMP] %s\r\n", p);
	    shut_down(0);
	}

	if (!prot_fgets(inputbuf, sizeof(inputbuf), popd_in)) {
	    shut_down(0);
	}

	p = inputbuf + strlen(inputbuf);
	if (p > inputbuf && p[-1] == '\n') *--p = '\0';
	if (p > inputbuf && p[-1] == '\r') *--p = '\0';

	/* Parse into keword and argument */
	for (p = inputbuf; *p && !isspace((int) *p); p++);
	if (*p) {
	    *p++ = '\0';
	    arg = p;
	    if (strcasecmp(inputbuf, "pass") != 0) {
		while (*arg && isspace((int) *arg)) {
		    arg++;
		}
	    }
	    if (!*arg) {
		if (strcasecmp(inputbuf, "auth") == 0) {
		    /* HACK for MS Outlook's incorrect use of the old-style
		     * SASL discovery method.
		     * Outlook uses "AUTH \r\n" instead if "AUTH\r\n"
		     */
		    arg = 0;
		}
		else {
		    prot_printf(popd_out, "-ERR Syntax error\r\n");
		    continue;
		}
	    }
	}
	else {
	    arg = 0;
	}
	lcase(inputbuf);

	if (!strcmp(inputbuf, "quit")) {
	    if (!arg) {
		if (popd_mailbox) {
		    if (!mailbox_lock_index(popd_mailbox)) {
		        int pollpadding =config_getint(IMAPOPT_POPPOLLPADDING);
			int minpollsec = config_getint(IMAPOPT_POPMINPOLL)*60;
		        if ((minpollsec > 0) && (pollpadding > 1)) { 
			    int mintime = popd_login_time - (minpollsec*(pollpadding));
			    if (popd_mailbox->pop3_last_login < mintime) {
			        popd_mailbox->pop3_last_login = mintime + minpollsec; 
			    } else {
			        popd_mailbox->pop3_last_login += minpollsec;
			    }
		        } else { 
			    popd_mailbox->pop3_last_login = popd_login_time;
		        }
			mailbox_write_index_header(popd_mailbox);
			mailbox_unlock_index(popd_mailbox);
		    }

		    for (msg = 1; msg <= popd_exists; msg++) {
			if (popd_msg[msg].deleted) break;
		    }

		    if (msg <= popd_exists) {
			(void) mailbox_expunge(popd_mailbox, expungedeleted,
					       0, 0);
			sync_log_mailbox(popd_mailbox->name);
		    }
		}
#ifdef APPLE_OS_X_SERVER
		odFreeUserOpts( gUserOpts, 0 );

		struct notify_message notifymsg;

		notifymsg.message = MASTER_SERVICE_STATUS_REMOVE_USER;
		notifymsg.service_pid = getpid();

		if ( write(STATUS_FD, &notifymsg, sizeof(notifymsg)) != sizeof(notifymsg ) )
		{
			syslog(LOG_ERR, "unable to tell master %x: %m", MASTER_SERVICE_STATUS_ADD_USER);
		}
#endif
		prot_printf(popd_out, "+OK\r\n");
		return;
	    }
	    else prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
	}
	else if (!strcmp(inputbuf, "capa")) {
	    if (arg) {
		prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
	    } else {
		cmd_capa();
	    }
	}
	else if (!strcmp(inputbuf, "stls") && tls_enabled()) {
	    if (arg) {
		prot_printf(popd_out,
			    "-ERR STLS doesn't take any arguments\r\n");
	    } else {
		cmd_starttls(0);
	    }
	}
	else if (!popd_mailbox) {
	    if (!strcmp(inputbuf, "user")) {
		if (!arg) {
		    prot_printf(popd_out, "-ERR Missing argument\r\n");
		}
		else {
		    cmd_user(arg);
		}
	    }
	    else if (!strcmp(inputbuf, "pass")) {
		if (!arg) prot_printf(popd_out, "-ERR Missing argument\r\n");
		else cmd_pass(arg);
	    }
	    else if (!strcmp(inputbuf, "apop") && *popd_apop_chal) {
		if (!arg) prot_printf(popd_out, "-ERR Missing argument\r\n");
		else cmd_apop(arg);
	    }
	    else if (!strcmp(inputbuf, "auth")) {
		cmd_auth(arg);
	    }
	    else {
		prot_printf(popd_out, "-ERR Unrecognized command\r\n");
	    }
	}
	else if (!strcmp(inputbuf, "stat")) {
	    unsigned nmsgs = 0, totsize = 0;
	    if (arg) {
		prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
	    }
	    else {
		for (msg = 1; msg <= popd_exists; msg++) {
		    if (!popd_msg[msg].deleted) {
			nmsgs++;
			totsize += popd_msg[msg].size;
		    }
		}
		prot_printf(popd_out, "+OK %u %u\r\n", nmsgs, totsize);
	    }
	}
	else if (!strcmp(inputbuf, "list")) {
	    if (arg) {
		msg = parsenum(&arg);
		if (arg) {
		    prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
		}
		else if (msg < 1 || msg > popd_exists ||
			 popd_msg[msg].deleted) {
		    prot_printf(popd_out, "-ERR No such message\r\n");
		}
		else {
		    prot_printf(popd_out, "+OK %u %u\r\n", msg, popd_msg[msg].size);
		}
	    }
	    else {
		prot_printf(popd_out, "+OK scan listing follows\r\n");
		for (msg = 1; msg <= popd_exists; msg++) {
		    if (!popd_msg[msg].deleted) {
			prot_printf(popd_out, "%u %u\r\n", msg, popd_msg[msg].size);
		    }
		}
		prot_printf(popd_out, ".\r\n");
	    }
	}
	else if (!strcmp(inputbuf, "retr")) {
	    if (!arg) prot_printf(popd_out, "-ERR Missing argument\r\n");
	    else {
		msg = parsenum(&arg);
		if (arg) {
		    prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
		}
		else if (msg < 1 || msg > popd_exists ||
			 popd_msg[msg].deleted) {
		    prot_printf(popd_out, "-ERR No such message\r\n");
		}
		else {
		    blat(msg, -1);
		}
	    }
	}
	else if (!strcmp(inputbuf, "dele")) {
	    if (!arg) prot_printf(popd_out, "-ERR Missing argument\r\n");
	    else if (config_popuseacl && !(mboxstruct.myrights & ACL_DELETEMSG)) {
		prot_printf(popd_out, "-ERR [SYS/PERM] %s\r\n",
			    error_message(IMAP_PERMISSION_DENIED));
	    }
	    else {
		msg = parsenum(&arg);
		if (arg) {
		    prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
		}
		else if (msg < 1 || msg > popd_exists ||
			 popd_msg[msg].deleted) {
		    prot_printf(popd_out, "-ERR No such message\r\n");
		}
		else {
		    popd_msg[msg].deleted = 1;
		    prot_printf(popd_out, "+OK message deleted\r\n");
		}
	    }
	}
	else if (!strcmp(inputbuf, "noop")) {
	    if (arg) {
		prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
	    }
	    else {
		prot_printf(popd_out, "+OK\r\n");
	    }
	}
	else if (!strcmp(inputbuf, "rset")) {
	    if (arg) {
		prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
	    }
	    else {
		for (msg = 1; msg <= popd_exists; msg++) {
		    popd_msg[msg].deleted = 0;
		}
		prot_printf(popd_out, "+OK\r\n");
	    }
	}
	else if (!strcmp(inputbuf, "top")) {
	    int lines;

	    if (arg) msg = parsenum(&arg);
	    if (!arg) prot_printf(popd_out, "-ERR Missing argument\r\n");
	    else {
		lines = parsenum(&arg);
		if (arg) {
		    prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
		}
		else if (msg < 1 || msg > popd_exists ||
			 popd_msg[msg].deleted) {
		    prot_printf(popd_out, "-ERR No such message\r\n");
		}
		else if (lines < 0) {
		    prot_printf(popd_out, "-ERR Invalid number of lines\r\n");
		}
		else {
		    blat(msg, lines);
		}
	    }
	}
	else if (!strcmp(inputbuf, "uidl")) {
	    if (arg) {
		msg = parsenum(&arg);
		if (arg) {
		    prot_printf(popd_out, "-ERR Unexpected extra argument\r\n");
		}
		else if (msg < 1 || msg > popd_exists ||
			 popd_msg[msg].deleted) {
		    prot_printf(popd_out, "-ERR No such message\r\n");
		}
		else if (mboxstruct.options & OPT_POP3_NEW_UIDL) {
			    prot_printf(popd_out, "+OK %u %lu.%u\r\n", msg, 
					mboxstruct.uidvalidity,
					popd_msg[msg].uid);
		}
		else {
		    /* old uidl format */
		    prot_printf(popd_out, "+OK %u %u\r\n", 
				msg, popd_msg[msg].uid);
		}
	    }
	    else {
		prot_printf(popd_out, "+OK unique-id listing follows\r\n");
		for (msg = 1; msg <= popd_exists; msg++) {
		    if (!popd_msg[msg].deleted) {
			if (mboxstruct.options & OPT_POP3_NEW_UIDL) {
			    prot_printf(popd_out, "%u %lu.%u\r\n", msg, 
					mboxstruct.uidvalidity,
					popd_msg[msg].uid);
			} else {
			    prot_printf(popd_out, "%u %u\r\n", msg, 
					popd_msg[msg].uid);
			}
		    }
		}
		prot_printf(popd_out, ".\r\n");
	    }
	}
	else {
	    prot_printf(popd_out, "-ERR Unrecognized command\r\n");
	}
    }		
}

#ifdef HAVE_SSL
static void cmd_starttls(int pop3s)
{
    int result;
    int *layerp;
    sasl_ssf_t ssf;
    char *auth_id;

    /* SASL and openssl have different ideas about whether ssf is signed */
    layerp = (int *) &ssf;

    if (popd_starttls_done == 1)
    {
	prot_printf(popd_out, "-ERR %s\r\n", 
		    "Already successfully executed STLS");
	return;
    }

    result=tls_init_serverengine("pop3",
				 5,        /* depth to verify */
				 !pop3s,   /* can client auth? */
				 !pop3s);  /* TLS only? */

    if (result == -1) {

	syslog(LOG_ERR, "[pop3d] error initializing TLS");

	if (pop3s == 0)
	    prot_printf(popd_out, "-ERR [SYS/PERM] %s\r\n", "Error initializing TLS");
	else
	    fatal("tls_init() failed",EC_TEMPFAIL);

	return;
    }

    if (pop3s == 0)
    {
	prot_printf(popd_out, "+OK %s\r\n", "Begin TLS negotiation now");
	/* must flush our buffers before starting tls */
	prot_flush(popd_out);
    }
  
    result=tls_start_servertls(0, /* read */
			       1, /* write */
			       layerp,
			       &auth_id,
			       &tls_conn);

    /* if error */
    if (result==-1) {
	if (pop3s == 0) {
	    prot_printf(popd_out, "-ERR [SYS/PERM] Starttls failed\r\n");
	    syslog(LOG_NOTICE, "[pop3d] STARTTLS failed: %s", popd_clienthost);
	} else {
	    syslog(LOG_NOTICE, "pop3s failed: %s", popd_clienthost);
	    fatal("tls_start_servertls() failed", EC_TEMPFAIL);
	}
	return;
    }

    /* tell SASL about the negotiated layer */
    result = sasl_setprop(popd_saslconn, SASL_SSF_EXTERNAL, &ssf);
    if (result != SASL_OK) {
	fatal("sasl_setprop() failed: cmd_starttls()", EC_TEMPFAIL);
    }
    saslprops.ssf = ssf;

    result = sasl_setprop(popd_saslconn, SASL_AUTH_EXTERNAL, auth_id);
    if (result != SASL_OK) {
        fatal("sasl_setprop() failed: cmd_starttls()", EC_TEMPFAIL);
    }
    if(saslprops.authid) {
	free(saslprops.authid);
	saslprops.authid = NULL;
    }
    if(auth_id)
	saslprops.authid = xstrdup(auth_id);

    /* tell the prot layer about our new layers */
    prot_settls(popd_in, tls_conn);
    prot_settls(popd_out, tls_conn);

    popd_starttls_done = 1;
}
#else
static void cmd_starttls(int pop3s __attribute__((unused)))
{
    fatal("cmd_starttls() called, but no OpenSSL", EC_SOFTWARE);
}
#endif /* HAVE_SSL */

static void cmd_apop(char *response)
{
    int sasl_result;
    char *canon_user;

    assert(response != NULL);

    if (popd_userid) {
	prot_printf(popd_out, "-ERR [AUTH] Must give PASS command\r\n");
	return;
    }

#ifdef APPLE_OS_X_SERVER
	if(!config_getswitch(IMAPOPT_POP_AUTH_APOP)){
	prot_printf(popd_out,"-ERR [AUTH] APOP not enabled\r\n");return;}

	if ( config_getswitch( IMAPOPT_POP_AUTH_APOP ) == 0 )
	{
		prot_printf(popd_out,"-ERR [AUTH] APOP not enabled\r\n");
		return;
	}

	if ( config_getswitch(IMAPOPT_APPLE_AUTH) == 0 )
	{
		sasl_result = sasl_checkapop(popd_saslconn,
					 popd_apop_chal,
					 strlen(popd_apop_chal),
					 response,
					 strlen(response));

		/*
		 * get the userid from SASL --- already canonicalized from
		 * mysasl_proxy_policy()
		 */
		sasl_result = sasl_getprop(popd_saslconn, SASL_USERNAME,
					   (const void **) &canon_user);
		if (sasl_result != SASL_OK) {
		prot_printf(popd_out, 
				"-ERR [AUTH] weird SASL error %d getting SASL_USERNAME\r\n", 
				sasl_result);
		return;
		}
		popd_userid = xstrdup(canon_user);
	}
	else
	{
		/* odCheckAPOP mallocs user and fills global user opts struct */
		sasl_result = odCheckAPOP( popd_apop_chal, response, gUserOpts );
		if ( sasl_result != eAODNoErr )
		{
			prot_printf( popd_out, "-ERR [AUTH] authentication error: %d\r\n", sasl_result );

			syslog( LOG_NOTICE, "badlogin: %s APOP (%s) Error: %d",
				popd_clienthost, popd_apop_chal, sasl_result );
			
			return;
		}

		if ( !(gUserOpts->fAccountState & eAccountEnabled) )
		{
			if ( gUserOpts->fAccountState & eACLNotMember )
			{
				syslog( LOG_NOTICE, "badlogin from: %s. APOP user: %s. service ACL is not enabled for this user",
						popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

				prot_printf( popd_out, "-ERR [SYS/PERM] mail service ACL is not enabled for this user\r\n" );
			}
			else if ( gUserOpts->fAccountState & eAutoForwardedEnabled )
			{
				syslog( LOG_NOTICE, "badlogin from: %s. APOP user: %s. auto-forwarding is enabled for this user",
						popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

				prot_printf( popd_out, "-ERR [SYS/PERM] mail auto-forwarding is enabled for this user\r\n" );
			}
			else
			{
				syslog( LOG_NOTICE, "badlogin from: %s. APOP user: %s. mail is not enabled for this user",
						popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

				prot_printf( popd_out, "-ERR [SYS/PERM] mail account is not enabled for this user\r\n" );
			}

			return;
		}

		if ( !(gUserOpts->fAccountState & ePOPEnabled) )
		{
			syslog( LOG_NOTICE, "badlogin: %s APOP user \"%s\" POP3 access is not enabled for this user",
					popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

			prot_printf( popd_out, "-ERR [SYS/PERM] NO POP3 access is not enabled for this user\r\n" );

			return;
		}

		/* successful authentication */
		canon_user = auth_canonifyid( gUserOpts->fRecNamePtr, 0 );

		popd_userid = xstrdup(canon_user);
	}
#else
    sasl_result = sasl_checkapop(popd_saslconn,
				 popd_apop_chal,
				 strlen(popd_apop_chal),
				 response,
				 strlen(response));
#endif
    
    /* failed authentication */
    if (sasl_result != SASL_OK)
    {
	syslog(LOG_NOTICE, "badlogin: %s APOP (%s) %s",
	       popd_clienthost, popd_apop_chal,
	       sasl_errdetail(popd_saslconn));
	
	sleep(3);      

	/* Don't allow user probing */
	if (sasl_result == SASL_NOUSER) sasl_result = SASL_BADAUTH;
		
	prot_printf(popd_out, "-ERR [AUTH] authenticating: %s\r\n",
		    sasl_errstring(sasl_result, NULL, NULL));

	if (popd_subfolder) {
	    free(popd_subfolder);
	    popd_subfolder = 0;
	}
	return;
    }

    /* successful authentication */

#ifndef APPLE_OS_X_SERVER
    /*
     * get the userid from SASL --- already canonicalized from
     * mysasl_proxy_policy()
     */
    sasl_result = sasl_getprop(popd_saslconn, SASL_USERNAME,
			       (const void **) &canon_user);
    popd_userid = xstrdup(canon_user);
    if (sasl_result != SASL_OK) {
	prot_printf(popd_out, 
		    "-ERR [AUTH] weird SASL error %d getting SASL_USERNAME\r\n", 
		    sasl_result);
	if (popd_subfolder) {
	    free(popd_subfolder);
	    popd_subfolder = 0;
	}
	return;
    }
#endif
    
    syslog(LOG_NOTICE, "login: %s %s%s APOP%s %s", popd_clienthost,
	   popd_userid, popd_subfolder ? popd_subfolder : "",
	   popd_starttls_done ? "+TLS" : "", "User logged in");

    popd_authstate = auth_newstate(popd_userid);

    openinbox();
}

void cmd_user(char *user)
{
    char userbuf[MAX_MAILBOX_NAME+1], *dot, *domain;
    unsigned userlen;

    /* possibly disallow USER */
    if (!(kflag || popd_starttls_done ||
	  config_getswitch(IMAPOPT_ALLOWPLAINTEXT))) {
	prot_printf(popd_out,
		    "-ERR [AUTH] USER command only available under a layer\r\n");
	return;
    }

    if (popd_userid) {
	prot_printf(popd_out, "-ERR [AUTH] Must give PASS command\r\n");
	return;
    }

#ifdef APPLE_OS_X_SERVER
	/* alloc global user opts struct if not already */
	if ( config_getswitch( IMAPOPT_POP_AUTH_CLEAR ) == 0 )
	{
		prot_printf( popd_out, "-ERR [AUTH] pass not enabled\r\n" );
		return;
	}

    if (popd_userid) {
	prot_printf(popd_out, "-ERR [AUTH] Must give PASS command\r\n");
	return;
    }
#endif
    if (popd_canon_user(popd_saslconn, NULL, user, 0,
			SASL_CU_AUTHID | SASL_CU_AUTHZID,
			NULL, userbuf, sizeof(userbuf), &userlen) ||
	     /* '.' isn't allowed if '.' is the hierarchy separator */
	     (popd_namespace.hier_sep == '.' && (dot = strchr(userbuf, '.')) &&
	      !(config_virtdomains &&  /* allow '.' in dom.ain */
		(domain = strchr(userbuf, '@')) && (dot > domain))) ||
	     strlen(userbuf) + 6 > MAX_MAILBOX_NAME) {
	prot_printf(popd_out, "-ERR [AUTH] Invalid user\r\n");
	syslog(LOG_NOTICE,
	       "badlogin: %s plaintext %s invalid user",
	       popd_clienthost, beautify_string(user));
    }
    else {
	popd_userid = xstrdup(userbuf);
	prot_printf(popd_out, "+OK Name is a valid mailbox\r\n");
#ifdef APPLE_OS_X_SERVER
	struct notify_message notifymsg;

	notifymsg.message = MASTER_SERVICE_STATUS_ADD_USER;
	notifymsg.service_pid = getpid();
	notifymsg.p_start_time = time( NULL );
	strlcpy( notifymsg.p_user_buf, popd_userid, sizeof( notifymsg.p_user_buf ) );
	strlcpy( notifymsg.p_host_buf, popd_clienthost, sizeof( notifymsg.p_host_buf ) );
	if ( write(STATUS_FD, &notifymsg, sizeof(notifymsg)) != sizeof(notifymsg ) )
	{
		syslog(LOG_ERR, "unable to tell master %x: %m", MASTER_SERVICE_STATUS_ADD_USER);
	}
	proc_register("pop3d", popd_clienthost, popd_userid, (char *)0 );
#endif
    }
}

void cmd_pass(char *pass)
{
#ifdef APPLE_OS_X_SERVER
    int bad_login	= 0;
#endif
    int plaintextloginpause;

    if (!popd_userid) {
	prot_printf(popd_out, "-ERR [AUTH] Must give USER command\r\n");
	return;
    }

#ifdef APPLE_OS_X_SERVER
	if(!config_getswitch(IMAPOPT_POP_AUTH_CLEAR)){
	prot_printf(popd_out,"-ERR [AUTH] pass not enabled\r\n"); return;}

	/* set global user options */
	odGetUserOpts( popd_userid, gUserOpts );
#endif

#ifdef HAVE_KRB
    if (kflag) {
	if (strcmp(popd_userid, kdata.pname) != 0 ||
	    kdata.pinst[0] ||
	    strcmp(klrealm, kdata.prealm) != 0) {
	    prot_printf(popd_out, "-ERR [AUTH] Invalid login\r\n");
	    syslog(LOG_NOTICE,
		   "badlogin: %s kpop %s %s%s%s@%s access denied",
		   popd_clienthost, popd_userid,
		   kdata.pname, kdata.pinst[0] ? "." : "",
		   kdata.pinst, kdata.prealm);
	    return;
	}

	syslog(LOG_NOTICE, "login: %s %s kpop", popd_clienthost, popd_userid);

	openinbox();
	return;
    }
#endif

    if (!strcmp(popd_userid, "anonymous")) {
	if (config_getswitch(IMAPOPT_ALLOWANONYMOUSLOGIN)) {
	    pass = beautify_string(pass);
	    if (strlen(pass) > 500) pass[500] = '\0';
	    syslog(LOG_NOTICE, "login: %s anonymous %s",
		   popd_clienthost, pass);
	}
	else {
	    syslog(LOG_NOTICE, "badlogin: %s anonymous login refused",
		   popd_clienthost);
	    prot_printf(popd_out, "-ERR [AUTH] Invalid login\r\n");
#ifdef APPLE_OS_X_SERVER
		odFreeUserOpts(gUserOpts, 0);
#endif
	    return;
	}
    }
#ifdef APPLE_OS_X_SERVER
    else if ( (config_getswitch(IMAPOPT_APPLE_AUTH) == 0) &&
			  (sasl_checkpass(popd_saslconn,
			    popd_userid,
			    strlen(popd_userid),
			    pass,
			    strlen(pass))!=SASL_OK) ) { 
#else
    else if (sasl_checkpass(popd_saslconn,
			    popd_userid,
			    strlen(popd_userid),
			    pass,
			    strlen(pass))!=SASL_OK) { 
#endif
	syslog(LOG_NOTICE, "badlogin: %s plaintext %s %s",
	       popd_clienthost, popd_userid, sasl_errdetail(popd_saslconn));
	sleep(3);
	prot_printf(popd_out, "-ERR [AUTH] Invalid login\r\n");
#ifdef APPLE_OS_X_SERVER
	odFreeUserOpts(gUserOpts, 0);
#endif
	free(popd_userid);
	popd_userid = 0;
	if (popd_subfolder) {
	    free(popd_subfolder);
	    popd_subfolder = 0;
	}
	return;
    }
#ifdef APPLE_OS_X_SERVER
	else if ( (config_getswitch( IMAPOPT_APPLE_AUTH )) &&
			  (odCheckPass( pass, gUserOpts )) != 0)
	{ 
		syslog(LOG_NOTICE, "badlogin: %s plaintext %s", popd_clienthost, popd_userid);

		sleep(3);
		prot_printf(popd_out, "-ERR [AUTH] Invalid login\r\n");
		odFreeUserOpts(gUserOpts, 0);
		free(popd_userid);
		popd_userid = 0;
	
		return;
	}
	else
	{
		if ( !(gUserOpts->fAccountState & eAccountEnabled) )
		{
			if ( gUserOpts->fAccountState & eACLNotMember )
			{
				syslog( LOG_NOTICE, "badlogin from: %s. plaintext user: %s. service ACL is not enabled for this user",
						popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

				prot_printf( popd_out, "-ERR [SYS/PERM] mail service ACL is not enabled for this user\r\n" );
			}
			else if ( gUserOpts->fAccountState & eAutoForwardedEnabled )
			{
				syslog( LOG_NOTICE, "badlogin from: %s. plaintext user: %s. auto-forwarding is enabled for this user",
						popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

				prot_printf( popd_out, "-ERR [SYS/PERM] mail auto-forwarding is enabled for this user\r\n" );
			}
			else
			{
				syslog( LOG_NOTICE, "badlogin from: %s. plaintext user: %s. mail is not enabled for this user",
						popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

				prot_printf( popd_out, "-ERR [SYS/PERM] mail account is not enabled for this user\r\n" );
			}

			odFreeUserOpts( gUserOpts, 0 );
			return;
		}

		if ( !(gUserOpts->fAccountState & ePOPEnabled) )
		{
			syslog( LOG_NOTICE, "badlogin: %s plaintext user \"%s\" POP3 access is not enabled for this user",
					popd_clienthost, beautify_string( gUserOpts->fRecNamePtr ) );

			prot_printf( popd_out, "-ERR [SYS/PERM] NO POP3 access is not enabled for this user\r\n" );

			odFreeUserOpts( gUserOpts, 0 );
			return;
		}

		syslog(LOG_NOTICE, "login: %s %s plaintext%s %s", popd_clienthost,
			   popd_userid, popd_starttls_done ? "+TLS" : "", 
			   "User logged in");
		if ((plaintextloginpause = config_getint(IMAPOPT_PLAINTEXTLOGINPAUSE))
			 != 0) {
			sleep(plaintextloginpause);
		}
    }

	if ( popd_userid )
	{
		free(popd_userid);
	}
	popd_userid = xstrdup( gUserOpts->fRecNamePtr );
#else
    else {
	syslog(LOG_NOTICE, "login: %s %s%s plaintext%s %s", popd_clienthost,
	       popd_userid, popd_subfolder ? popd_subfolder : "",
	       popd_starttls_done ? "+TLS" : "", "User logged in");

	if ((plaintextloginpause = config_getint(IMAPOPT_PLAINTEXTLOGINPAUSE))
	     != 0) {
	    sleep(plaintextloginpause);
	}
    }
#endif

    popd_authstate = auth_newstate(popd_userid);

    openinbox();
}

/* Handle the POP3 Extension extension.
 */
void cmd_capa()
{
    int minpoll = config_getint(IMAPOPT_POPMINPOLL) * 60;
    int expire = config_getint(IMAPOPT_POPEXPIRETIME);
    int mechcount;
    const char *mechlist;

    prot_printf(popd_out, "+OK List of capabilities follows\r\n");

#ifdef APPLE_OS_X_SERVER
	if ( !config_getswitch( IMAPOPT_APPLE_AUTH ) )
	{
		/* SASL special case: print SASL, then a list of supported capabilities */
		if (!popd_mailbox && !backend &&
		sasl_listmech(popd_saslconn,
				  NULL, /* should be id string */
				  "SASL ", " ", "\r\n",
				  &mechlist,
				  NULL, &mechcount) == SASL_OK && mechcount > 0) {
		prot_write(popd_out, mechlist, strlen(mechlist));
		}
	}
	else if ( config_getswitch( IMAPOPT_APPLE_AUTH ) )
	{
		if ( config_getswitch( IMAPOPT_POP_AUTH_APOP ) || config_getswitch( IMAPOPT_POP_AUTH_GSSAPI ) )
		{
			prot_printf(popd_out, "SASL ");
			if ( config_getswitch( IMAPOPT_POP_AUTH_APOP ) )
			{
				prot_printf(popd_out, "APOP ");
			}
			if ( config_getswitch( IMAPOPT_POP_AUTH_GSSAPI ) )
			{
				prot_printf(popd_out, "GSSAPI");
			}
			prot_printf(popd_out, "\r\n");
		}
	}
#else
    /* SASL special case: print SASL, then a list of supported capabilities */
    if (!popd_mailbox && !backend &&
	sasl_listmech(popd_saslconn,
		      NULL, /* should be id string */
		      "SASL ", " ", "\r\n",
		      &mechlist,
		      NULL, &mechcount) == SASL_OK && mechcount > 0) {
	prot_write(popd_out, mechlist, strlen(mechlist));
    }
#endif

    if (tls_enabled() && !popd_starttls_done && !popd_mailbox && !backend) {
	prot_printf(popd_out, "STLS\r\n");
    }
    if (expire < 0) {
	prot_printf(popd_out, "EXPIRE NEVER\r\n");
    } else {
	prot_printf(popd_out, "EXPIRE %d\r\n", expire);
    }

    prot_printf(popd_out, "LOGIN-DELAY %d\r\n", minpoll);
    prot_printf(popd_out, "TOP\r\n");
    prot_printf(popd_out, "UIDL\r\n");
    prot_printf(popd_out, "PIPELINING\r\n");
    prot_printf(popd_out, "RESP-CODES\r\n");
    prot_printf(popd_out, "AUTH-RESP-CODE\r\n");

    if (!popd_mailbox && !backend &&
	(kflag || popd_starttls_done
	 || config_getswitch(IMAPOPT_ALLOWPLAINTEXT))) {
	prot_printf(popd_out, "USER\r\n");
    }
    
    prot_printf(popd_out,
		"IMPLEMENTATION Cyrus POP3%s server %s\r\n",
		config_mupdate_server ? " Murder" : "", CYRUS_VERSION);

    prot_printf(popd_out, ".\r\n");
    prot_flush(popd_out);
}


void cmd_auth(char *arg)
{
    int r, sasl_result;
    char *authtype;
    char *canon_user;

    /* if client didn't specify an argument we give them the list
     *
     * XXX This method of mechanism discovery is an undocumented feature
     * that appeared in draft-myers-sasl-pop3 and is still used by
     * some clients.
     */
    if (!arg) {
	const char *sasllist;
	int mechnum;

	prot_printf(popd_out, "+OK List of supported mechanisms follows\r\n");
      
	/* CRLF separated, dot terminated */
	if (sasl_listmech(popd_saslconn, NULL,
			  "", "\r\n", "\r\n",
			  &sasllist,
			  NULL, &mechnum) == SASL_OK) {
	    if (mechnum>0) {
		prot_printf(popd_out,"%s",sasllist);
	    }
	}
      
	prot_printf(popd_out, ".\r\n");
      	return;
    }

    authtype = arg;

    /* according to RFC 2449, since we advertise the "SASL" capability, we
     * must accept an optional second argument as an initial client
     * response (base64 encoded!).
     */ 
    while (*arg && !isspace((int) *arg)) {
	arg++;
    }
    if (isspace((int) *arg)) {
	/* null terminate authtype, get argument */
	*arg++ = '\0';
    } else {
	/* no optional client response */
	arg = NULL;
    }

    r = saslserver(popd_saslconn, authtype, arg, "", "+ ", "",
		   popd_in, popd_out, &sasl_result, NULL);

    if (r) {
	const char *errorstring = NULL;

	switch (r) {
	case IMAP_SASL_CANCEL:
	    prot_printf(popd_out,
			"-ERR [AUTH] Client canceled authentication\r\n");
	    break;
	case IMAP_SASL_PROTERR:
	    errorstring = prot_error(popd_in);

	    prot_printf(popd_out,
			"-ERR [AUTH] Error reading client response: %s\r\n",
			errorstring ? errorstring : "");
	    break;
	default:
	    /* failed authentication */
	    if (authtype) {
		syslog(LOG_NOTICE, "badlogin: %s %s %s",
		       popd_clienthost, authtype,
		       sasl_errstring(sasl_result, NULL, NULL));
	    } else {
		syslog(LOG_NOTICE, "badlogin: %s %s",
		       popd_clienthost, authtype);
	    }

	    sleep(3);

	    /* Don't allow user probing */
	    if (sasl_result == SASL_NOUSER) sasl_result = SASL_BADAUTH;
		
	    prot_printf(popd_out, "-ERR [AUTH] authenticating: %s\r\n",
			sasl_errstring(sasl_result, NULL, NULL));
	}
	
	if (popd_subfolder) {
	    free(popd_subfolder);
	    popd_subfolder = 0;
	}
	reset_saslconn(&popd_saslconn);
	return;
    }

    /* successful authentication */

    /* get the userid from SASL --- already canonicalized from
     * mysasl_proxy_policy()
     */
    sasl_result = sasl_getprop(popd_saslconn, SASL_USERNAME,
			       (const void **) &canon_user);
    if (sasl_result != SASL_OK) {
	prot_printf(popd_out, 
		    "-ERR [AUTH] weird SASL error %d getting SASL_USERNAME\r\n", 
		    sasl_result);
	return;
    }

    /* If we're proxying, the authzid may contain a subfolder,
       so re-canonify it */
    if (config_getswitch(IMAPOPT_POPSUBFOLDERS) && strchr(canon_user, '+')) {
	char userbuf[MAX_MAILBOX_NAME+1];
	unsigned userlen;

	sasl_result = popd_canon_user(popd_saslconn, NULL, canon_user, 0,
				      SASL_CU_AUTHID | SASL_CU_AUTHZID,
				      NULL, userbuf, sizeof(userbuf), &userlen);
	if (sasl_result != SASL_OK) {
	    prot_printf(popd_out, 
			"-ERR [AUTH] SASL canonification error %d\r\n", 
			sasl_result);
	    return;
	}

	popd_userid = xstrdup(userbuf);
    } else {
	popd_userid = xstrdup(canon_user);
    }
    syslog(LOG_NOTICE, "login: %s %s%s %s%s %s", popd_clienthost,
	   popd_userid, popd_subfolder ? popd_subfolder : "",
	   authtype, popd_starttls_done ? "+TLS" : "", "User logged in");

    if (!openinbox()) {
	prot_setsasl(popd_in,  popd_saslconn);
	prot_setsasl(popd_out, popd_saslconn);
    }
    else {
	reset_saslconn(&popd_saslconn);
    }
}

/*
 * Complete the login process by opening and locking the user's inbox
 */
int openinbox(void)
{
    char userid[MAX_MAILBOX_NAME+1], inboxname[MAX_MAILBOX_PATH+1];
    char extname[MAX_MAILBOX_NAME+1] = "INBOX";
    int type, myrights = 0;
    char *server = NULL, *acl;
    int r, log_level = LOG_ERR;
    const char *statusline = NULL;

    /* Translate any separators in userid
       (use a copy since we need the original userid for AUTH to backend) */
    strlcpy(userid, popd_userid, sizeof(userid));
    mboxname_hiersep_tointernal(&popd_namespace, userid,
				config_virtdomains ?
				strcspn(userid, "@") : 0);

    /* Create the mailbox that we're trying to access */
    if (popd_subfolder && popd_subfolder[1]) {
	snprintf(extname+5, sizeof(extname)-5, "%c%s",
		 popd_namespace.hier_sep, popd_subfolder+1);
    }
    r = (*popd_namespace.mboxname_tointernal)(&popd_namespace, extname,
					      userid, inboxname);

#ifdef APPLE_OS_X_SERVER
	/* create inbox */
	if ( !r ) {
	char *partition	= NULL;
	if ( (gUserOpts != NULL) && gUserOpts->fAltDataLocPtr != NULL ) {
		partition = gUserOpts->fAltDataLocPtr;
	}
	mboxlist_createmailbox( inboxname, MAILBOX_FORMAT_NORMAL, partition, 1, popd_userid, NULL, 0, 0, 0 );
	}
#endif

    if (!r) r = mboxlist_detail(inboxname, &type, NULL, NULL,
				&server, &acl, NULL);
    if (!r && (config_popuseacl = config_getswitch(IMAPOPT_POPUSEACL)) &&
	(!acl ||
	 !((myrights = cyrus_acl_myrights(popd_authstate, acl)) & ACL_READ))) {
	r = (myrights & ACL_LOOKUP) ?
	    IMAP_PERMISSION_DENIED : IMAP_MAILBOX_NONEXISTENT;
	log_level = LOG_INFO;
    }
    if (r) {
	sleep(3);
	syslog(log_level, "Unable to locate maildrop %s: %s",
	       inboxname, error_message(r));
	prot_printf(popd_out,
		    "-ERR [SYS/PERM] Unable to locate maildrop: %s\r\n",
		    error_message(r));
	goto fail;
    }

    if (type & MBTYPE_REMOTE) {
	/* remote mailbox */

	/* xxx hide the fact that we are storing partitions */
	if (server) {
	    char *c;
	    c = strchr(server, '!');
	    if(c) *c = '\0';
	}

	/* Make a working copy of userid in case we need to alter it */
	strlcpy(userid, popd_userid, sizeof(userid));

	if (popd_subfolder) {
	    /* Add the subfolder back to the userid for proxying */
	    size_t n = strlen(popd_subfolder);
	    char *p = (config_virtdomains && (p = strchr(userid, '@'))) ?
		p : userid + strlen(userid);
	    memmove(p+n, p, strlen(p)+1);
	    memcpy(p, popd_subfolder, n);
	}

	backend = backend_connect(NULL, server, &protocol[PROTOCOL_POP3],
				  userid, NULL, &statusline);

	if (!backend) {
	    syslog(LOG_ERR, "couldn't authenticate to backend server");
	    prot_printf(popd_out, "-ERR%s",
			statusline ? statusline :
			" Authentication to backend server failed\r\n");
	    prot_flush(popd_out);
	    
	    goto fail;
	}
    }
    else {
	/* local mailbox */
	int msg;
	struct index_record record;
	int minpoll;
	int doclose = 0;

	popd_login_time = time(0);

	r = mailbox_open_header(inboxname, popd_authstate, &mboxstruct);
	if (!r) {
	    doclose = 1;
	    if (config_popuseacl && !(mboxstruct.myrights & ACL_READ)) {
		r = (mboxstruct.myrights & ACL_LOOKUP) ?
		    IMAP_PERMISSION_DENIED : IMAP_MAILBOX_NONEXISTENT;
		log_level = LOG_INFO;
	    }
	}
	if (r) {
	    sleep(3);
	    syslog(log_level, "Unable to open maildrop %s: %s",
		   inboxname, error_message(r));
	    prot_printf(popd_out,
			"-ERR [SYS/PERM] Unable to open maildrop: %s\r\n",
			error_message(r));
	    if (doclose) mailbox_close(&mboxstruct);
	    goto fail;
	}

	r = mailbox_open_index(&mboxstruct);
	if (!r) r = mailbox_lock_pop(&mboxstruct);
	if (r) {
	    mailbox_close(&mboxstruct);
	    syslog(LOG_ERR, "Unable to lock maildrop %s: %s",
		   inboxname, error_message(r));
	    prot_printf(popd_out,
			"-ERR [IN-USE] Unable to lock maildrop: %s\r\n",
			error_message(r));
	    goto fail;
	}

	if ((minpoll = config_getint(IMAPOPT_POPMINPOLL)) &&
	    mboxstruct.pop3_last_login + 60*minpoll > popd_login_time) {
	    prot_printf(popd_out,
			"-ERR [LOGIN-DELAY] Logins must be at least %d minute%s apart\r\n",
			minpoll, minpoll > 1 ? "s" : "");
	    mailbox_close(&mboxstruct);
	    goto fail;
	}

	if (!r) {
	    popd_exists = mboxstruct.exists;
	    popd_msg = (struct msg *) xrealloc(popd_msg, (popd_exists+1) *
					       sizeof(struct msg));
	    for (msg = 1; msg <= popd_exists; msg++) {
		if ((r = mailbox_read_index_record(&mboxstruct, msg, &record))!=0)
		    break;
		popd_msg[msg].uid = record.uid;
		popd_msg[msg].size = record.size;
		popd_msg[msg].deleted = 0;
	    }
	}
	if (r) {
	    mailbox_close(&mboxstruct);
	    popd_exists = 0;
	    syslog(LOG_ERR, "Unable to read maildrop %s", inboxname);
	    prot_printf(popd_out,
			"-ERR [SYS/PERM] Unable to read maildrop\r\n");
	    goto fail;
	}
	popd_mailbox = &mboxstruct;
#ifdef APPLE_OS_X_SERVER
	struct notify_message notifymsg;

	notifymsg.message = MASTER_SERVICE_STATUS_ADD_USER;
	notifymsg.service_pid = getpid();
	notifymsg.p_start_time = time( NULL );
	strlcpy( notifymsg.p_user_buf, popd_userid, sizeof( notifymsg.p_user_buf ) );
	strlcpy( notifymsg.p_host_buf, popd_clienthost, sizeof( notifymsg.p_host_buf ) );
	if ( write(STATUS_FD, &notifymsg, sizeof(notifymsg)) != sizeof(notifymsg ) )
	{
		syslog(LOG_ERR, "unable to tell master %x: %m", MASTER_SERVICE_STATUS_ADD_USER);
	}
#endif
	proc_register("pop3d", popd_clienthost, popd_userid,
		      popd_mailbox->name);
    }

    /* Create telemetry log */
    popd_logfd = telemetry_log(popd_userid, popd_in, popd_out, 0);

    prot_printf(popd_out, "+OK%s",
		statusline ? statusline : " Mailbox locked and ready\r\n");
    prot_flush(popd_out);
    return 0;

  fail:
#ifdef APPLE_OS_X_SERVER
	odFreeUserOpts(gUserOpts, 0);
#endif
    free(popd_userid);
    popd_userid = 0;
    if (popd_subfolder) {
	free(popd_subfolder);
	popd_subfolder = 0;
    }
    auth_freestate(popd_authstate);
    popd_authstate = NULL;
    return 1;
}

static void blat(int msg,int lines)
{
    FILE *msgfile;
    char buf[4096];
    char fnamebuf[MAILBOX_FNAME_LEN];
    int thisline = -2;

    strlcpy(fnamebuf, popd_mailbox->path, sizeof(fnamebuf));
    strlcat(fnamebuf, "/", sizeof(fnamebuf));
    mailbox_message_get_fname(popd_mailbox, popd_msg[msg].uid,
			      fnamebuf + strlen(fnamebuf),
			      sizeof(fnamebuf) - strlen(fnamebuf));
    msgfile = fopen(fnamebuf, "r");
    if (!msgfile) {
	prot_printf(popd_out, "-ERR [SYS/PERM] Could not read message file\r\n");
	return;
    }
    prot_printf(popd_out, "+OK Message follows\r\n");
    while (lines != thisline) {
	if (!fgets(buf, sizeof(buf), msgfile)) break;

	if (thisline < 0) {
	    if (buf[0] == '\r' && buf[1] == '\n') thisline = 0;
	}
	else thisline++;

	if (buf[0] == '.') prot_putc('.', popd_out);
	do {
	    prot_printf(popd_out, "%s", buf);
	}
	while (buf[strlen(buf)-1] != '\n' && fgets(buf, sizeof(buf), msgfile));
    }
    fclose(msgfile);

    /* Protect against messages not ending in CRLF */
    if (buf[strlen(buf)-1] != '\n') prot_printf(popd_out, "\r\n");

    prot_printf(popd_out, ".\r\n");

    /* Reset inactivity timer in case we spend a long time
       pushing data to the client over a slow link. */
    prot_resettimeout(popd_in);
}

static int parsenum(char **ptr)
{
    char *p = *ptr;
    int result = 0;

    if (!isdigit((int) *p)) {
	*ptr = 0;
	return -1;
    }
    while (*p && isdigit((int) *p)) {
	result = result * 10 + *p++ - '0';
        if (result < 0) {
            /* xxx overflow */
        }
    }

    if (*p) {
	while (*p && isspace((int) *p)) p++;
	*ptr = p;
    }
    else *ptr = 0;
    return result;
}

static int expungedeleted(struct mailbox *mailbox __attribute__((unused)),
			  void *rock __attribute__((unused)), char *index,
			  int expunge_flags __attribute__((unused)))
{
    int msg;
    int uid = ntohl(*((bit32 *)(index+OFFSET_UID)));

    for (msg = 1; msg <= popd_exists; msg++) {
	if (popd_msg[msg].uid == uid) {
	    return popd_msg[msg].deleted;
	}
    }
    return 0;
}

/* Reset the given sasl_conn_t to a sane state */
static int reset_saslconn(sasl_conn_t **conn) 
{
    int ret;
    sasl_security_properties_t *secprops = NULL;

    sasl_dispose(conn);
    /* do initialization typical of service_main */
    ret = sasl_server_new("pop", config_servername,
                         NULL, NULL, NULL,
                         NULL, 0, conn);
    if(ret != SASL_OK) return ret;

    if(saslprops.ipremoteport)
       ret = sasl_setprop(*conn, SASL_IPREMOTEPORT,
                          saslprops.ipremoteport);
    if(ret != SASL_OK) return ret;
    
    if(saslprops.iplocalport)
       ret = sasl_setprop(*conn, SASL_IPLOCALPORT,
                          saslprops.iplocalport);
    if(ret != SASL_OK) return ret;
    secprops = mysasl_secprops(SASL_SEC_NOPLAINTEXT);
    ret = sasl_setprop(*conn, SASL_SEC_PROPS, secprops);
    if(ret != SASL_OK) return ret;
    /* end of service_main initialization excepting SSF */

    /* If we have TLS/SSL info, set it */
    if(saslprops.ssf) {
	ret = sasl_setprop(*conn, SASL_SSF_EXTERNAL, &saslprops.ssf);
    } else {
	ret = sasl_setprop(*conn, SASL_SSF_EXTERNAL, &extprops_ssf);
    }

    if(ret != SASL_OK) return ret;

    if(saslprops.authid) {
       ret = sasl_setprop(*conn, SASL_AUTH_EXTERNAL, saslprops.authid);
       if(ret != SASL_OK) return ret;
    }
    /* End TLS/SSL Info */

    return SASL_OK;
}

/* we've authenticated the client, we've connected to the backend.
   now it's all up to them */
static void bitpipe(void)
{
    struct protgroup *protin = protgroup_new(2);
    int shutdown = 0;
    char buf[4096];

    protgroup_insert(protin, popd_in);
    protgroup_insert(protin, backend->in);

    do {
	/* Flush any buffered output */
	prot_flush(popd_out);
	prot_flush(backend->out);

	/* check for shutdown file */
	if (shutdown_file(buf, sizeof(buf))) {
	    shutdown = 1;
	    goto done;
	}
    } while (!proxy_check_input(protin, popd_in, popd_out,
				backend->in, backend->out, 0));

 done:
    /* ok, we're done. */
    protgroup_free(protin);

    if (shutdown) {
	char *p;
	for (p = buf; *p == '['; p++); /* can't have [ be first char */
	prot_printf(popd_out, "-ERR [SYS/TEMP] %s\r\n", p);
	shut_down(0);
    }

    return;
}


void printstring(const char *s __attribute__((unused)))
{
    /* needed to link against annotate.o */
    fatal("printstring() executed, but its not used for POP3!",
	  EC_SOFTWARE);
}
