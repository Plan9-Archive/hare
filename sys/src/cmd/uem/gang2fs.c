/*
	gangfs - aggregated execution management

	Copyright (C) 2010, IBM Corporation, 
 		Eric Van Hensbergen (bergevan@us.ibm.com)

	Description:
		This provides a file system which interacts with execfs(4)
		to provide a gang execution environment.  This initial version
		will be single-node, but the intention is to support multinode
		environments

	Based in part on Pravin Shinde's devtask.c

	TODO:
		* per stdio aggregation mode
		* non-aggregation mode for stdio(s)
		* push helper: (launch local stage1 and stage3 with fanout stage2)
		* session level status file
		* MAYBE: aggregated status via multipipes
		* nextaggregated wait via multipipe barrier (todo in multipipe)
*/

#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <mp.h>
#include <libsec.h>
#include <stdio.h>
#include "debug.h"

int remote_override = 0;
int remote = 0;
int ntask_override = 0;

static char defaultpath[] =	"/proc";
static char defaultsrvpath[] =	"gangfs";
static char defaultsrvmpipe[] =	"mpipe";
static char *procpath, *execfspath, *srvpath, *srvmpipe;
int iothread_id;

static char *gangpath;	/* our view of the gang path */
static char *xgangpath; /* external view of the gang path */
static char *mysysname;
static char *srvctl;
static Channel *iochan;
static Channel *clunkchan;
static int updateinterval; /* in seconds */
static char *amprefix = "/n"; /* mount prefix */

char Enotdir[] = "Not a directory";
char Enotfound[] = "File not found";
char Eperm[] = "Permission denied";
char Enogang[] = "ctl file has no gang";
char Ebusy[] = "gang already has subsessions reserved";
char Ebadimode[] = "unknown input mode";
char Ebadpid[] = "stupid pid problem";
char Ebadctl[] = "problemn reading session ctl";
char Estdin[] = "problem binding gang stdin";
char Estdout[] = "problem binding gang stdout";
char Estderr[] = "problem binding gang stderr";
char Enores[] = "insufficient resources";
char Echeckmount[] = "could not check mount";
char Eclosing[] = "gang is closing or broken";
char Estatfind[] = "could not allocate or find child";
char Ebadstat[] = "bad status message";
char Estatparse[] = "problem parsing status write";
char Eattach[] = "invalid attach specifier";
char Enotimp[] = "not implemented yet";
char Etolong[] = "ctl message too long";

enum
{
	STACK = (8 * 1024),
	NAMELEN = 255,
	NUMSIZE = 12,
};

/* 
  qid.path format:
	[MAGIC(16)][CONV(16)][TYPE(16)] where low 00ff bytes are type
*/
#define MAGIC		 ((uvlong)0x6AF5<<32)
#define TYPE(x) 	 ((ulong) (x).path & 0xff)
#define CONVP(x) 	 ((((ulong) (x).path >> 16)&0xffff) -1)
#define CONV(x) 	 ((((ulong) (x)->path >> 16)&0xffff) -1)
#define CONVQIDP(c, y) ((uvlong)(MAGIC|((((c+1) << 16)|(y)))))

typedef struct Session Session;
typedef struct Gang Gang;
struct Session
{
	Gang *g;		/* gang? necessary? */
	char *path;		/* path to exec gang */
	int fd;			/* fd to ctl file */
	Req *r;			/* outstanding ctl requests */
	Channel *chan;	/* status channel */
	int pid;		/* Session's actual pid */
	int index;		/* session index in gang */
	int remote;		/* flag marking whether session is local or remote */
	int subindex;		/* sub index into session  */
};

enum {
	GANG_NEW,		/* allocated */
	GANG_RESV,		/* reserved */
	GANG_EXEC,		/* executing */
	GANG_CLOSING,	/* closing */
	GANG_CLOSED,	/* closed */
	GANG_BROKE,		/* broken */
};

/* NOTE:
   we use the structural reference count for clean up
   of the struct and the ctl ref count for the clean
   up of the directory
*/
struct Gang
{
	char *path;		/* gang's path */
	int refcount;	/* structural reference count */
	int ctlref;		/* control file reference count */
	int status;		/* current status of gang */

	int index;		/* gang id */
	int size;		/* number of subsessions */
	Session *sess;	/* array of subsessions */
	int imode;		/* input mode (enum/bcast/raw) */

	Channel *chan;	/* channel for sync/error reporting */

	Gang *next;		/* primary linked list */
};

RWLock glock; 
Gang *glist;

// FIXME: comment...

typedef struct Dirtab Dirtab;
struct Dirtab
{
	char name[NAMELEN];
	Qid qid;
	long perm;
};

enum
{
	Qroot = 0,
	Qclone,
	Qgstat,
	Qrootend,
	Qconv = 0,
	Qctl,
	Qenv,
	Qns,
	Qargs,
	Qstatus,
	Qwait,
	Qconvend,
	Qsubses = Qconvend,
};

/* these dirtab entries form the base */
static Dirtab rootdir[] = {
	"gclone", 	{Qclone},				0666,
	"status",	{Qgstat},				0666,
};

static Dirtab convdir[] = {
	"ctl",		{Qctl},					0666,
	"env",		{Qenv},					0444,
	"ns",		{Qns},					0444,
	"args",		{Qargs},				0444,
	"status",	{Qstatus},				0444,
	"wait",		{Qwait},				0444,
};

enum
{
	CMenum,			/* set input to enum (must be before res) */
	CMbcast,		/* set input to broadcast mode (must be before res) */
	CMres,			/* reserve a set of nodes */
	CMmount,		/* backmount peer */
};

Cmdtab ctlcmdtab[]={
	CMenum, "enum", 1,
	CMbcast, "bcast", 1,
	CMres, "res", 0,
	CMmount, "checkmount", 2,
};

#define	MAXNUM	10	/* maximum number of numbers on data line */

enum
{
	/* old /dev/swap */
	Mem		= 0,
	Maxmem,
	Swap,
	Maxswap,

	/* /dev/sysstats */
	Procno	= 0,
	Context,
	Interrupt,
	Syscall,
	Fault,
	TLBfault,
	TLBpurge,
	Load,
	Idle,
	InIntr,
};

/* BUG: we'll need a lock to protect the linked list */
typedef struct Status Status;
struct Status
{
	char	name[16];
	long	lastup;	/* last time this was updated */

	int		ntask;  /* the number of tasks (on a remote
				   gange, it is the sum of all its children) */
	int nchild; /* number of children */
	int njobs; /* numer of tasks being run */

	int		statsfd;
	int		swapfd;

	uvlong	devswap[4];
	uvlong	devsysstat[10];

	/* big enough to hold /dev/sysstat even with many processors */
	char	buf[8*1024]; // FIXME: check to make sure this is still true */
	char	*bufp;
	char	*ebufp;

	Status	*next;	/* linked list of children */
};

static Status avgstats;	/* composite statistics */
static char *canonpath; /* canonical path */
static Status mystats;	/* my statistics */
RWLock statslock; /* protect stat updates and additions */ 

/* FIXME: debugging... */
static void
lsproc(void *arg)
{
	threadsetname("gangfs-lsproc");
	DPRINT(DFID, "lsproc (%s) (%s) (%s) (%s) pid=(%d)\n",
	       "/bin/ls", "ls", "-l", arg, getpid());
	procexecl(nil, "/bin/ls", "ls", "-l", arg, nil);

	threadexits(nil);

}

static void
dprint_status(Status *s, char *who, int dev)
{
	DPRINT(DSTS, "%s: name=(%s)\n", who, s->name);
	DPRINT(DSTS, "\tntask=(%d)\n", s->ntask);
	DPRINT(DSTS, "\tnchild=(%d)\n", s->nchild);
	DPRINT(DSTS, "\tnjobs=(%d)\n", s->njobs);

	if(dev){
		DPRINT(DSTS, "\tdevswap[Mem]=(%d)\n", s->devswap[Mem]);
		DPRINT(DSTS, "\tdevswap[Maxmem]=(%d)\n", s->devswap[Maxmem]);
		DPRINT(DSTS, "\tdevsysstat[Load]=(%d)\n", s->devsysstat[Load]);
		DPRINT(DSTS, "\tdevsysstat[Idle]=(%d)\n", s->devsysstat[Idle]);
	}
}

static int
loadbuf(Status *m, int *fd)
{
	int n;

	if(*fd < 0)
		return 0;
	seek(*fd, 0, 0);
	n = read(*fd, m->buf, sizeof m->buf-1);
	if(n <= 0){
		close(*fd);
		*fd = -1;
		return 0;
	}
	m->bufp = m->buf;
	m->ebufp = m->buf+n;
	m->buf[n] = 0;
	return 1;
}

/* read one line of text from buffer and process integers */
static int
readnums(Status *m, int n, uvlong *a, int spanlines)
{
	int i;
	char *p, *ep;

	if(spanlines)
		ep = m->ebufp;
	else
		for(ep=m->bufp; ep<m->ebufp; ep++)
			if(*ep == '\n')
				break;
	p = m->bufp;
	for(i=0; i<n && p<ep; i++){
		while(p<ep && (!isascii(*p) || !isdigit(*p)) && *p!='-')
			p++;
		if(p == ep)
			break;
		a[i] = strtoull(p, &p, 10);
	}
	if(ep < m->ebufp)
		ep++;
	m->bufp = ep;
	return i == n;
}

/* read one line of text from buffer and process integers */
static int
readints(char *buf, int n, int *a)
{
	int i;
	char *p, *ep;

	p = buf;
	ep = buf+strlen(buf);

	for(i=0; i<n && p<ep; i++){
		while(p<ep && (!isascii(*p) || !isdigit(*p)) && *p!='-')
			p++;
		if(p == ep)
			break;
		a[i] = strtol(p, &p, 10);
	}
	if(i != n) {
		DPRINT(DERR, "i=%d, n=%d\n", i, n);
	}
	return i == n;
}


int
readnum(ulong off, char *buf, ulong n, ulong val, int size)
{
	char tmp[64];

	snprint(tmp, sizeof(tmp), "%*.*lud", size-1, size-1, val);
	tmp[size-1] = ' ';
	if(off >= size)
		return 0;
	if(off+n > size)
		n = size-off;
	memmove(buf, tmp+off, n);
	return n;
}

static int
readswap(Status *m, uvlong *a)
{
	if(strstr(m->buf, "memory\n")){
		/* new /dev/swap - skip first 3 numbers */
		if(!readnums(m, 7, a, 1))
			return 0;
		a[0] = a[3];
		a[1] = a[4];
		a[2] = a[5];
		a[3] = a[6];
		return 1;
	}
	return readnums(m, nelem(m->devswap), a, 0);
}

static int
refreshstatus(Status *m)
{
	int n;
	int i;
	uvlong a[nelem(m->devsysstat)];
	
	wlock(&statslock);
	// FIXME: this might not work when we are dealing with gangs
	// of gangs because it is both a child and a master.

	if(m->next) { /* aggregation node */
		Status *c;

		/* go through all the children and add the important bits up */
		m->ntask = 0;
		m->nchild = 0;
		m->njobs = 0; /* MAYBE: do we really want to do this? */
		m->devswap[Mem] = 0;
		m->devswap[Maxmem] = 0;
		m->devsysstat[Load] = 0;
		m->devsysstat[Idle] = 0;
		c = m->next;
		while(c != nil) {
			dprint_status(c, "refreshstatus (child)", 0);

			m->ntask += c->ntask;
			/* if child is an aggregation node only count its children */
			if(c->nchild) { 
				m->nchild += c->nchild;
			} else {			
				m->nchild++;
			}

			m->njobs += c->njobs;
			m->devswap[Mem] += c->devswap[Mem];
			m->devswap[Maxmem] += c->devswap[Maxmem];
			m->devsysstat[Load] += c->devsysstat[Load];
			m->devsysstat[Idle] =+ c->devsysstat[Idle];
			c = c->next;
		}

		/* MAYBE: average load and idle when done? */
	} else { /* child node */
		if(loadbuf(m, &m->swapfd) && readswap(m, a))
			memmove(m->devswap, a, sizeof m->devswap);

		if(loadbuf(m, &m->statsfd)){
			memset(m->devsysstat, 0, sizeof m->devsysstat);
			for(n=0; n<m->ntask && readnums(m, nelem(m->devsysstat), a, 0); n++)
				for(i=0; i<nelem(m->devsysstat); i++)
					m->devsysstat[i] += a[i];
		}
	}

	m->lastup = time(0);

	wunlock(&statslock);

	// what's going on...
	dprint_status(m, "refreshstatus (master after agregarion)", 1);

	return 1;
}

static void
fillstatbuf(Status *m, char *statbuf)
{
	char *bp;
	ulong t;

	DPRINT(DSTS, "fillstatbuf:\n");
	bp = statbuf;
	t = time(0);

	if(t-m->lastup > updateinterval)
		refreshstatus(m);

	// what's going on...
	dprint_status(m, "fillstatbuf (after refres)", 1);

	rlock(&statslock);
	bp+=snprint(bp, 17, "%-15.15s ", mystats.name);

	readnum(0, bp, NUMSIZE, m->ntask, NUMSIZE);
	bp+=NUMSIZE;
	readnum(0, bp, NUMSIZE, m->nchild, NUMSIZE);
	bp+=NUMSIZE;
	readnum(0, bp, NUMSIZE, m->njobs, NUMSIZE);
	bp+=NUMSIZE;	
	readnum(0, bp, NUMSIZE, m->devsysstat[Load], NUMSIZE);
	bp+=NUMSIZE;
	readnum(0, bp, NUMSIZE, m->devsysstat[Idle], NUMSIZE);
	bp+=NUMSIZE;
	readnum(0, bp, NUMSIZE, m->devswap[Mem], NUMSIZE);
	bp+=NUMSIZE;
	readnum(0, bp, NUMSIZE, m->devswap[Maxmem], NUMSIZE);
	runlock(&statslock);

	bp+=NUMSIZE;
	*bp = ' '; /* space at the end to help parse things */
	bp++;
	*bp = 0;	
}

static Status *
findchild(Status *m, char *name)
{	
	Status *current = m;
	
	rlock(&statslock);
	while(current != nil) {
		if(strcmp(name, current->name) == 0) 
			break;
		current = current->next;
	}
	runlock(&statslock);
	return current;
}

/* find lowest loaded child for next workload */
/* NOTE: right now we just base this on number of jobs */
static Status *
idlechild(Status *m)
{
	Status *current = m->next;
	Status *lowest = current;

	rlock(&statslock);
	while(current != nil) {
		if(current->njobs < lowest->njobs)
			lowest = current;
		current = current->next;
	}
	runlock(&statslock);

	return lowest;
}

static char *
statuswrite(char *buf)
{
	char name[16];
	Status *m;
	int ret;
	char *bp;
	int a[7];
	
	/* grab name and match it to our children */
	strecpy(name, name+sizeof name, buf);
	/* trunk name to first space */
	bp = strchr(name, ' ');
	if(bp != 0)
		*bp = 0;

	DPRINT(DSTS, "statuswrite: name=(%s)\n", name);

	m = findchild(&mystats, name);
	if(m == nil) {
		DPRINT(DSTS, "\tcreating new status for (%s)\n", name);
		m = emalloc9p(sizeof(Status));
		// FIXME: this was moved from below to make more sense.  Probably not the correct error...
		if(m == nil)
			return Estatfind;
		memset(m, 0, sizeof(Status));
		strncpy(m->name, name, 16);
		wlock(&statslock);
		m->next = mystats.next;
		mystats.next = m;
		mystats.nchild++;
		wunlock(&statslock);
	}


	ret = readints(buf+16, 7, a);
	if(ret == 0) {
		DPRINT(DERR, "statuswrite: readints returned %d\n", ret);
		return Estatparse;
	} else {
		m->ntask = a[0];
		m->nchild = a[1];
		m->njobs = a[2];
		m->devsysstat[Load] = a[3];
		m->devsysstat[Idle] = a[4];
		m->devswap[Mem] = a[5];
		m->devswap[Maxmem] = a[6];
	}

	m->lastup = time(0); 
/* MAYBE: pull timestamp from child and include it in data */
/* FIXME: see if this causes problems with the BG runs
	if(xgangpath == procpath){
		xgangpath = smprint("%s/%s%s", amprefix, mysysname, procpath);
		DPRINT(DEXE, "statuswrite: reseting xgangpath=(%s)\n", xgangpath);
	}
	DPRINT(DSTS, "statuswrite: xgangpath=(%s)\n", xgangpath);
*/
	return nil;
}

static void
updatestatus(void *arg)
{
	int n;
	char *parent = (char *) arg;
	char *statbuf = emalloc9p((NUMSIZE*11+1) + 1);
	char *parentpath = smprint("%s/%s/proc/status", amprefix, parent);

	int parentfd = open(parentpath, OWRITE);
			
	DPRINT(DCUR, "updatestatus: parent=(%s) parentpath=(%s)\n", parent, parentpath);
	DPRINT(DCUR, "\tparentfd=(%d)\n", parentfd);
	if(parentfd < 0) {
		DPRINT(DERR, "*ERROR*: couldn't open parent %s on path %s\n", parent, parentpath);
		return;
	}
	
	/* TODO: set my priority low */ 

	for(;;) {
		fillstatbuf(&mystats, statbuf);
		n = fprint(parentfd, "%s\n", statbuf);
		if(n < 0)
			break;
		sleep(updateinterval*1000);
	}

	// FIXME: shouldn't parentfd be closed at the end?
	close(parentfd);

	free(parentpath);
	free(statbuf);
}

static void
initstatus(Status *m)
{
	int n;
	uvlong a[nelem(m->devsysstat)];

	DPRINT(DEXE, "initstatus:\n");
	memset(m, 0, sizeof(Status));
	strncpy(m->name, mysysname, 15);
	m->name[15] = 0;
	m->swapfd = open("/dev/swap", OREAD);
	DPRINT(DCUR, "\tm->swapfd=(%d)\n", m->swapfd);
	assert(m->swapfd > 0);
	m->statsfd = open("/dev/sysstat", OREAD);
	DPRINT(DCUR, "\tm->statsfd=(%d)\n", m->statsfd);
	assert(m->swapfd > 0);
	if(loadbuf(m, &m->statsfd)){
		for(n=0; readnums(m, nelem(m->devsysstat), a, 0); n++)
			;
	if(ntask_override)
		m->ntask = ntask_override;
	else
		m->ntask = n;
	} else
		m->ntask = 1;
	m->lastup = time(0);

	dprint_status(m, "initstatus:", 0);
}

static void
closestatus(Status *m)
{
	close(m->statsfd);
	close(m->swapfd);
}

/* remote support */
static void
startimport(void *arg)
{
	char *addr = (char *) arg;
	char *name = smprint("%s/%s", amprefix, addr);

	threadsetname("gangfs-startimport");
	
	// FIXME: should arg be replaced with addr
	DPRINT(DCUR, "startimport: importing name=%s\n");
	procexecl(nil, "/bin/import", "import", "-A", arg, "/",
		  name, nil);

	DPRINT(DERR, "*ERROR*: startimport: procexecl returned: %r\n");
	threadexits(nil);
}

/* checkmount: check to see if a remote system is already, if not mount it */
static int
checkmount(char *addr)
{
	char *dest = estrdup9p(addr);
	int retries = 0;
	int err = 0;
	int fd;
	char *srvpt = smprint("/srv/%s", addr);
	char *srvtg = smprint("%s/%s", amprefix, addr);
	char *mtpt = smprint("%s/proc", srvtg);
	Dir *tmp = dirstat(mtpt);

	DPRINT(DCUR, "checkmount: checking mount point (%s)\n", srvtg);

	if(tmp != nil) {
		DPRINT(DEXE, "\tsrv already mounted on (%s)\n", mtpt);
		goto out;
	} else 

	DPRINT(DFID, "\tmount point (%s) does not exist... mounting\n", srvtg);
	DPRINT(DCUR, "\tattempting to mount %s on %s\n", srvpt, srvtg);
	fd = open(srvpt, ORDWR);
	if(fd > 0) {
		DPRINT(DCUR, "\tfd=(%d)\n", fd);
		if(mount(fd, -1, srvtg, MREPL, "") < 0) {
			// FIXME: what should we do here?
			DPRINT(DERR, "\t*ERROR*: srv mount failed: %r\n");
			close(fd);
		} else {
			DPRINT(DFID, "\tsrv mount succeeded");
			DPRINT(DFID, "\t\tMOUNT");
			close(fd);
			goto out;
		}
	}

	/* no dir, spawn off an import */
	DPRINT(DFID, "\tno dir, spawn off an import +++\n");
	proccreate(startimport, dest, STACK);

	/* we need to wait for this to be done */
	while((tmp = dirstat(mtpt)) == nil) {
		// start a mount with a timeout...
		// use wait....
		if(retries++ > 100) {
			DPRINT(DERR, "*ERROR*: checkmount: import not complete after 10 seconds, giving up\n");
			sleep(100);
			err = -1;
			break;
		}
	}

out:
	if(srvpt)
		free(srvpt);
	if(srvtg)
		free(srvtg);
	if(tmp)
		free(tmp);
	free(dest);
	free(mtpt);
	return err;
}	

static void
usage(void)
{
	fprint(2, "gangfs [-D] [-v debug_level] [-p parent] [-s server_name] [-m mtpt] [-E execfs_mtpt] [-n mysysname] [-i interval]\n");

	exits("usage");
}

static void
gangref(Gang *g, char *where)
{
	g->refcount++;
	DPRINT(DREF, "\t\tGang(%p).refcount = %d [%s]\n", g, g->refcount, where);
}

static void
gangrefinc(Gang *g, char *where)
{
	g->refcount++;
	DPRINT(DREF, "\t\tGang(%p).refcount++ = %d -> %d[%s]\n", g, g->refcount-1, g->refcount, where);
}

static void
gangrefdec(Gang *g, char *where)
{
	g->refcount--;
	// reality check
	if(g->refcount < 0) {
		DPRINT(DERR, "%s: *WARNING* gangrefdec is less than 0.  Resetting to 0\n", where);
		g->refcount = 0;	
	}
	DPRINT(DREF, "\t\tGang(%p).refcount-- = %d -> %d[%s]\n", g, g->refcount+1, g->refcount, where);
}

static int
mpipe(char *path, char *name)
{
	int fd, ret;
	// FIXME: is the cost/savings benifit of creating srvpt
	// each time necessary?  Look at changing srvmpipe to 
	// /srv/mpipe, etc. and use it.
	char *srvpt = smprint("/srv/%s", srvmpipe);
	fd = open(srvpt, ORDWR);
	if(fd<0) {
		DPRINT(DERR, "*ERROR*: couldn't open %s: %r\n", srvpt);
		free(srvpt);
		return -1;
	}
	free(srvpt);

	//wunlock(&glock);
	DPRINT(DFID, "mpipe: mounting name=(%s) on path=(%s) pid=(%d) fd=%d\n",
	       name, path, getpid(), fd);
	DPRINT(DCUR, "\tfrom mysysname=(%s)\n", mysysname);
	ret = mount(fd, -1, path, MBEFORE, name);
	//wunlock(&glock);
	if(ret < 0) {
		DPRINT(DERR, "mount of multipipe failed: %r\n");
	} else
		DPRINT(DFID, "\t\tMOUNT");

	close(fd);
	return ret;
}

static int
flushmp(char *src)
{
	int fd = open(src, OWRITE);
	int n; 
	char hdr[255];
	ulong tag = ~0; /* header byte is at offset ~0 */
	char pkttype='.';

	if(fd < 0) {
		DPRINT(DPIP, "flushmp: open failed: %r\n");
		return fd;
	}

	n = snprint(hdr, 255, "%c\n%lud\n%lud\n%s\n",
		    pkttype, (ulong)0, (ulong)0, "");
	DPRINT(DCUR, "flushmp: src=(%s) pid=(%d)\n", src, getpid());
	DPRINT(DCUR, "\tfd=(%d)\n", fd);

	n = pwrite(fd, hdr, n, tag);
	close(fd);
	return n;
}

static int
spliceto(char *src, char *dest)
{
	int fd = open(src, OWRITE);
	int n; 
	char hdr[255];
	ulong tag = ~0; /* header byte is at offset ~0 */
	char pkttype='>';

	if(fd < 0) {
		DPRINT(DPIP, "spliceto: open %s failed: %r\n", src);
		return fd;
	}

	DPRINT(DCUR, "spliceto: fd=(%d)\n", fd);
	n = snprint(hdr, 255, "%c\n%lud\n%lud\n%s\n", pkttype, (ulong)0, (ulong)0, dest);
	n = pwrite(fd, hdr, n, tag);
	close(fd);
	return n;
}

static int
splicefrom(char *dest, char *src)
{
	int fd = open(dest, OWRITE);
	int n; 
	char hdr[255];
	ulong tag = ~0; /* header byte is at offset ~0 */
	char pkttype='<';

	if(fd < 0) {
		DPRINT(DERR, "*ERROR*: splicefrom: open %s failed: %r\n", dest);
		return fd;
	}

	DPRINT(DCUR, "splicefrom: fd=(%d)\n", fd);
	DPRINT(DEXE, "just so we know where we are splicing: [%s]\n", src);

	n = snprint(hdr, 255, "%c\n%lud\n%lud\n%s\n", pkttype, (ulong)0, (ulong)0, src);
	n = pwrite(fd, hdr, n, tag);
	close(fd);
	return n;
}

static Gang *
newgang(void) 
{
	Gang *mygang = emalloc9p(sizeof(Gang));

	if(mygang == nil)
		return nil;
	memset(mygang, 0, sizeof(Gang));
	mygang->chan = chancreate(sizeof(void *), 0);

	wlock(&glock);
	if(glist == nil) {
		glist = mygang;
		mygang->index = 0;
	} else {
		int last = -1;
		Gang *current;
		for(current=glist; current->next != nil; current = current->next) {
			if(current->index != last+1) {
				break;
			}
			last++;
		}
		mygang->next = current->next;
		current->next = mygang;
		mygang->index = current->index+1;
	}

	mygang->ctlref = 1; 
	mygang->refcount = 1;
	mygang->imode = CMbcast;	/* broadcast mode default */
	mygang->status = GANG_NEW;
	wunlock(&glock);

	return mygang;
}

static Gang *
findgang(int index)
{
	Gang *current = nil;

	rlock(&glock);
	if(glist == nil) {
		goto out;
	} else {
		for(current=glist; current != nil; current = current->next) {
			if((current->index == index)&&(current->status != GANG_CLOSED)) {
				gangrefinc(current, "findgang");
				goto out;
			}
		}
	}
out:
	runlock(&glock);
	return current;
}

static Gang *
findgangnum(int which)
{
	Gang *current = nil;
	rlock(&glock);
	if(glist == nil) {
		goto out;
	} else {
		int count = 0;
		for(current=glist; current != nil; current = current->next) {
			if(current->status == GANG_CLOSED)
				continue;
			if(count == which) {
				gangrefinc(current, "findgangnum");
				goto out;
			}
			count++;
		}
	}
out:
	runlock(&glock);
	return current;
}

static void
releasesessions(Gang *g)
{
	int count;
	
	DPRINT(DEXE, "releasesessions:\n");
	for(count = 0; count < g->size; count++) {
		DPRINT(DEXE, "\tsess[%d].path=(%s)\n", count, g->sess[count].path);
		DPRINT(DEXE, "\tsess[%d].chan=(%d)\n", count, g->sess[count].chan);
		DPRINT(DEXE, "\tsess[%d].fd=(%d)\n", count, g->sess[count].fd);
		free(g->sess[count].path);
		chanfree(g->sess[count].chan);
		close(g->sess[count].fd);
	}

	free(g->sess);
	g->sess = nil;
}

static void
flushgang(void *arg)
{
	Gang *g = arg;
	char fname[255];

	DPRINT(DCLN, "flushgang: flushing stdin, stdout, and stderr\n");
	snprint(fname, 255, "%s/g%d/stdin", gangpath, g->index);
	flushmp(fname); /* flush pipe to be sure */
	snprint(fname, 255, "%s/g%d/stdout", gangpath, g->index);
	flushmp(fname);
	snprint(fname, 255, "%s/g%d/stderr", gangpath, g->index);
	flushmp(fname);
}

static void
printgang(Gang *g)
{
	rlock(&glock);

	DPRINT(DCUR, "\tprintgang:\n");
	DPRINT(DCUR, "\t\tpath=%s\n", g->path);
	DPRINT(DCUR, "\t\trefcount=%d\n", g->refcount);
	DPRINT(DCUR, "\t\tctlref=%d\n", g->ctlref);
	DPRINT(DCUR, "\t\tstatus=%d\n", g->status);
	DPRINT(DCUR, "\t\tindex=%d\n", g->index);
	DPRINT(DCUR, "\t\tsize=%d\n", g->size);
	DPRINT(DCUR, "\t\timode=%d\n", g->imode);
	DPRINT(DCUR, "\t\t*** sessions not reported\n");

	runlock(&glock);
}

static void
printganglist(void)
{
	Gang *g;
	
	DPRINT(DCUR, "printganglist:\n");
	for(g = glist; g != nil; g = g->next)
		printgang(g);
}

static void
cleanupgang(void *arg)
{
	Gang *g = arg;
	char fname[255];

	/* make sure that the std files are completely flushed */
	flushgang(arg);

	/* unmount the std files and remove their references */
	DPRINT(DCLN, "cleanupgang: unmounting stdin, stdout, and stderr\n");
	snprint(fname, 255, "%s/g%d/stdin", gangpath, g->index);
	if(unmount(0, fname) == -1)
		DPRINT(DERR, "cleanupgang: *ERROR* unable to unmount %s: %r\n", fname);
	gangrefdec(g, "cleanupgang");

	snprint(fname, 255, "%s/g%d/stdout", gangpath, g->index);
	if(unmount(0, fname) == -1)
		DPRINT(DERR, "cleanupgang: *ERROR* unable to unmount %s: %r\n", fname);
	gangrefdec(g, "cleanupgang");

	snprint(fname, 255, "%s/g%d/stderr", gangpath, g->index);
	if(unmount(0, fname) == -1)
		DPRINT(DERR, "cleanupgang: *ERROR* unable to unmount %s: %r\n", fname);
	gangrefdec(g, "cleanupgang");

	/* TODO: Clean up subsessions? */
	
	/* close it out */
	g->status=GANG_CLOSED;
printgang(g);
}

static void
releasegang(Gang *g)
{
	wlock(&glock);
	DPRINT(DEXE, "releasegang:\n");
//	gangrefdec(g, "releasegang"); // new gang reserved it so we
//			//need to decrement the ref counter here
	if(g->refcount == 0) {	/* clean up */
		Gang *c;
		Gang *p = glist;

		if(glist == g) {
			glist = g->next;
		} else {
			for(c = glist; c != nil; c = c->next) {
				if(c == g) {
					p->next = g->next;
					break;
				}
				p = c;
			}
		}
		
		releasesessions(g);
		chanfree(g->chan);
		free(g);
	}

	wunlock(&glock);
}

static void
cleanup(Srv *)
{
	DPRINT(DCUR, "cleanup: pid=(%d)\n", getpid());
	nbsendp(iochan, 0); /* flush any blocked readers */

	// FIXME: is this necessary?  the iothread is being held open...
	DPRINT(DFID, "cleanup: iothread's pid=(%d)\n", threadpid(iothread_id));
	threadkill(iothread_id);

	chanclose(iochan);
	closestatus(&mystats);
	sleep(5);
	DPRINT(DCUR, "cleanup: killing remaing threads\n");
	threadexitsall("done");
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, Eattach);
		return;
	}
	r->fid->qid.path = Qroot;
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static int
dirgen(int n, Dir *d, void *aux)
{
	Fid *f = aux;
	Gang *g = f->aux;
	Qid *q = &f->qid;
	Dirtab *dt;
	int ne = 0;
	int s = 0;

	USED(s);
	USED(ne);

	d->atime = time(nil);
	d->mtime = d->atime;
	d->uid = estrdup9p("gangfs");
	d->gid = estrdup9p("gangfs");
	d->muid = estrdup9p("gangfs");
	d->length = 0;

	if(n==-1) {
		if(CONV(q) == -1) { /* root directory */
			d->qid.path=CONVQIDP(-1, Qroot);
			d->qid.type=QTDIR;
			d->mode = 0555|DMDIR;
			d->name = smprint("#gtaskfs");
			return 0;			
		} else {			/* conv directory */
			d->qid.path=CONVQIDP(CONV(q), Qconv);
			d->qid.type=QTDIR;
			d->mode = 0555|DMDIR;
			d->name = smprint("g%lud", CONV(q)); 
			return 0;			
		}
	}

	if(CONV(q) == -1) { /* root directory */
		dt = rootdir;
		ne = Qrootend-1;
		s = n-ne;
		if(s >= 0) {
			g = findgangnum(s);
			if(g != nil) {
				d->qid.path=CONVQIDP(g->index, Qconv);
				d->qid.type=QTDIR;
				d->mode = 0555|DMDIR; /* FUTURE: set perms */
				d->name = smprint("g%d", g->index);
				releasegang(g);
				return 0;
			}
		}
	} else {				/* session directory */
		dt = convdir;
		ne = Qconvend-1;
		s = n-ne;

		if(g && (s >= 0) && (s < g->size)) {
			d->qid.path=CONVQIDP(CONV(q), Qsubses+s);
			d->qid.vers=0;
			d->qid.type=QTDIR;
			d->mode = DMDIR;	/* mount points only! */
			d->name = smprint("%d", s);
			return 0;
		}
	}

	if(n < ne) {	/* core synthetics */
		d->qid.path = CONVQIDP(CONV(q), dt[n].qid.path);
		d->mode = dt[n].perm;
		d->name = estrdup9p(dt[n].name);
		return 0;
	}

	return -1;
}

static int
isanumber(char *thingy)
{
	if(thingy == nil)
		return 0;
	if((thingy[0] >=  '0')&&(thingy[0] <= '9'))
		return 1;
	return 0;
}

static void
qidcopy(Qid *src, Qid *dst)
{
	dst->path = src->path;
	dst->vers = src->vers;
	dst->type = src->type;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Dirtab *dt;
	Gang *g = fid->aux;
	int j;

	if(!(fid->qid.type&QTDIR))
		return Enotdir;

	if(strcmp(name, "..") == 0){
		if((CONV(&fid->qid) == -1) || (TYPE(fid->qid) == Qconv)) {
			qid->path = Qroot;
			qid->type = QTDIR;
			qidcopy(qid, &fid->qid);
			return nil;
		} else {
			qid->path = CONVQIDP(CONV(&fid->qid), Qconv);
			qid->type = QTDIR;
			qidcopy(qid, &fid->qid);
			return nil;
		}	
	}
	/* depending on path do something different */
	if(CONV(&fid->qid) == -1) {
		dt = rootdir;
		for(j = 0; j < Qrootend; j++)
			if(strcmp(dt[j].name, name) == 0) {
				qidcopy(&dt[j].qid, qid);
				qidcopy(qid, &fid->qid);
				return nil;
			}
		if( (name[0] == 'g') && (isanumber(name+1))) { /* session  */
			int s = atoi(name+1);
			Gang *g = findgang(s);
			if(g != nil) {
				qid->path = CONVQIDP(g->index, Qconv);
				qid->vers = 0;
				qid->type = QTDIR;
				qidcopy(qid, &fid->qid);
				fid->aux = g;
				return nil;
			}
		}
	} else {
		dt = convdir;
		for(j = Qconv; j < Qconvend; j++)
			if(strcmp(dt[j].name, name) == 0) {
				qidcopy(&dt[j].qid, qid);
				qid->path = CONVQIDP(CONV(&fid->qid), qid->path);
				qidcopy(qid, &fid->qid);
				return nil;
			}
		if(isanumber(name)) {
			int s = atoi(name);
			if(g && (s < g->size)) {
				qid->path = CONVQIDP(CONV(&fid->qid), s+Qsubses);
				qid->vers = 0;
				qid->type = QTDIR;
				qidcopy(qid, &fid->qid);
				return nil;
			}		
		}	
	}
		
	return Enotfound;
}

static char *
fsclone(Fid *oldfid, Fid *newfid)
{
	Gang *g;

	DPRINT(DEXE, "fsclone: mysysname=(%s) \n", mysysname);
	if((oldfid == nil)||(newfid==nil)) {
		fprint(2, "fsclone: bad fids\n");
		return nil;
	}

	if(oldfid->aux == nil)
		return nil;
	if(newfid->aux != nil) {
		fprint(2, "newfid->aux is bit nil when oldfid->aux was not\n");
		return nil;
	}
	/* this will only work if only sessions use aux */
	g = oldfid->aux;
	if(g) {
		newfid->aux = oldfid->aux;
		gangrefinc(g, "fsclone");
	}

	return nil;
}

static void
fsopen(Req *r)
{
	ulong path;
	Fid *fid = r->fid;
	Qid *q = &fid->qid;
	Gang *mygang;

	if(TYPE(fid->qid) >= Qsubses) {
		DPRINT(DERR, "fsopen: ERROR: TYPE(fid->qid) >= Qsubses: %r\n");
		respond(r, Eperm);
		return;
	}
	if (q->type&QTDIR){
		// FIXME: this is probably not an error -- 
		// DPRINT(DERR, "fsopen: ERROR: q->type&QTDIR: %r\n");
		respond(r, nil);
		return;
	}
	
	if(TYPE(fid->qid) == Qctl) {
		mygang = fid->aux;
		DPRINT(DFID, "fsopen: mygang=(%p)\n", mygang);
		
		if(mygang) {
			wlock(&glock);
			if(mygang->status >= GANG_CLOSING) {
				wunlock(&glock);
				respond(r, Eclosing);
				return;
			} else {
				mygang->ctlref++;
			}
			wunlock(&glock);
		} else
			// it is only an error when we are not cloning
			if(TYPE(fid->qid) != Qclone)
				DPRINT(DERR, "*ERROR*: fsopen: ctl fid %d without aux\n", fid->fid);
	}

	if(TYPE(fid->qid) != Qclone) {
		/* FUTURE: probably need better ref count here */
		respond(r, nil);
		return;
	}

	DPRINT(DGAN, "\t newgang %p\n", r);
	/* insert new session at the end of the list & assign index*/
	mygang = newgang();
	if(mygang == nil) {
		respond(r, Enores);
		return;
	}
	fid->aux = mygang;
	
	path = CONVQIDP(mygang->index, Qctl);
	r->fid->qid.path = path;
	r->ofcall.qid.path = path;

	respond(r, nil);
}

static void
fsread(Req *r)
{
	Fid *fid = r->fid;
	Qid *q = &fid->qid;
	char buf[NAMELEN];
	char *statbuf;

	if (q->type&QTDIR) {
		dirread9p(r, dirgen, fid);
		respond(r, nil);
		return;
	}

	switch(TYPE(fid->qid)) {
		case Qctl:
			sprint(buf, "%lud", CONVP(fid->qid));
			readstr(r, buf);
			respond(r, nil);
			return;
		case Qgstat:
			statbuf=emalloc9p((NUMSIZE*11+1) + 1);
			fillstatbuf(&mystats, statbuf);
			readstr(r, statbuf);
			respond(r, nil);
			free(statbuf);
			return;
		default:
			respond(r, Enotimp);
	}
}

/* mypath is the canonical path to my gproc */
/* path is the path to the target proc */
static char *
setupstdio(Session *s)
{
	Gang *g = s->g;
	char buf[255];
	char dest[255];
	int n;

	DPRINT(DEXE, "setupstdio: path=(%s) remote=(%d)\n", s->path, s->remote);
	DPRINT(DEXE, "\tgangpath=(%s) xgangpath=(%s)\n", gangpath, xgangpath);

	/* BUG: won't work for gang because gang pid is not an int */
	if(s->remote)
		snprint(buf, 255, "%s/g%d", s->path, s->pid);
	else
		snprint(buf, 255, "%s/%d", s->path, s->pid);

	snprint(dest, 255, "%s/g%d/%d", gangpath, g->index, s->index);
	n = bind(buf, dest, MREPL);
	DPRINT(DEXE, "\tbind buf=(%s) dest=(%s) ret=%d\n", buf, dest, n);
	if(n < 0)
		return smprint("couldn't bind %s %s: %r\n", buf, dest);

// FIXME: the rest of the gangs are actually xgangs...
	if(s->remote)
		snprint(buf, 255, "%s/g%d/stdin", s->path, s->pid);
	else
		snprint(buf, 255, "%s/%d/stdin", s->path, s->pid);
	snprint(dest, 255, "%s/g%d/stdin", xgangpath, g->index);
	DPRINT(DEXE, "\tsplicefrom buf=(%s) dest=(%s)\n", buf, dest);
	n = splicefrom(buf, dest); /* execfs initiates splicefrom gangfs */
	DPRINT(DEXE, "\t\tret=%d\n", n);
	if(n < 0)
		return smprint("setupstdio: %r\n");

	if(s->remote)
		snprint(buf, 255, "%s/g%d/stdout", s->path, s->pid);
	else
		snprint(buf, 255, "%s/%d/stdout", s->path, s->pid);
	snprint(dest, 255, "%s/g%d/stdout", xgangpath, g->index);
	DPRINT(DEXE, "\tspliceto buf=(%s) dest=(%s)\n", buf, dest);
	n = spliceto(buf, dest); /* execfs initiates spliceto gangfs stdout */
	DPRINT(DEXE, "\t\tret=%d\n", n);
	if(n < 0)
		return smprint("setupstdio: %r\n");

	if(s->remote)
		snprint(buf, 255, "%s/g%d/stderr", s->path, s->pid);
	else
		snprint(buf, 255, "%s/%d/stderr", s->path, s->pid);
	snprint(dest, 255, "%s/g%d/stderr", xgangpath, g->index);
	DPRINT(DEXE, "\tspliceto buf=(%s) dest=(%s)\n", buf, dest);
	n = spliceto(buf, dest); /* execfs initiates spliceto gangfs stderr */
	DPRINT(DEXE, "\t\tret=%d\n", n);
	if(n < 0)
		return smprint("setupstdio: %r\n");

	return nil;
}

/* reserve a session using clone semantics */
/* NOTE: Should work with execfs or gangfs */
static void
cloneproc(void *arg)
{
	Session *sess = (Session *) arg;
	char buf[256];
	int retries;
	int n = -1;
	char *err;

	if(sess->remote)
		snprint(buf, 255, "%s/gclone", sess->path);
	else
		snprint(buf, 255, "%s/clone", sess->path);

	DPRINT(DEXE, "cloneproc: **** clone proc %s (remote=%d)\n",
	       buf, sess->remote);
	for(retries=0; retries <= 10; retries++) {
		sess->fd = open(buf, ORDWR);

		if(sess->fd <= 0) {
			sleep (100);
			DPRINT(DERR, "WARNING: retry: %d opening execfs returned %d: %r -- retrying\n", 
			       retries, sess->fd);
			continue;
		}

		n = pread(sess->fd, buf, 255, 0);
		if(n >= 0)
			break;

		DPRINT(DERR, "WARNING: retry: %d execfs's pread=(%d) failed for fd=(%d): %r -- retrying\n", 
		       retries, n, sess->fd);
		close (sess->fd);
	}
	DPRINT(DCUR, "\tcontrol channel on sess->fd=(%d) for subsession (%d) pread=(%d)\n",
	       sess->fd, sess->subindex, n);

	if(retries > 10) {
		DPRINT(DERR, "*ERROR* giving up\n");
		sendp(sess->chan, smprint("execfs returning %r"));
		threadexits(Ebadpid);
	}

	if(n < 0) {
		sendp(sess->chan,
			smprint("couldn't read from session ctl path=%s/clone fd=(%d) n=(%d): %r\n", 
				sess->path, sess->fd, n));
		threadexits(Ebadctl);
	}
	buf[n] = 0; // FIXME: need to terminate the buffer or we get junk...

	if(sess->remote)
		sess->pid = atoi(buf+1); /* skip the g */
	else 
		sess->pid = atoi(buf); /* convert to local execs session number */

	DPRINT(DEXE, "cloneproc: sess->pid=%ld buf=(%s)\n", sess->pid, buf);

	/* if we are a remote session, setup backmount */
	if(sess->remote) {
		DPRINT(DEXE, "Attempting to remote checkmount %s on %s\n", 
		       mysysname, sess->path);
		n = fprint(sess->fd, "checkmount %s", mysysname);
		if(n < 0) {
			DPRINT(DEXE, "setting up backmount failed from %s to %s: %r\n",
			       mysysname, sess->path);
			chanprint(sess->chan, "setting up backmount failed from %s to %s: %r\n",
			       mysysname, sess->path);
			goto error;
		}

		if(sess->g->imode == CMenum) {
			n = fprint(sess->fd, "enum");
			if(n < 0) {
				DPRINT(DEXE, "setting up enum failed on %s: %r\n", mysysname);
				chanprint(sess->chan, "setting up enum failed on %s: %r\n",
					mysysname);
				goto error;
			}
		}

		n = fprint(sess->fd, "res %d", sess->remote);
		if(n < 0) {
			DPRINT(DEXE, "remote reservation failed: %r\n");
			chanprint(sess->chan, "remote reservation failed: %r\n");
			goto error;
		}
	}

	/* finish by setting up stdio */
	if(err = setupstdio(sess)) {
		DPRINT(DEXE, "reporting failure from stdio setup session %d:%d: %s\n", 
				sess->index, sess->subindex, err);
		sendp(sess->chan, err);
	} else {
		DPRINT(DEXE, "reporting success from session %d:%d\n", sess->index, sess->subindex);
		sendp(sess->chan, nil);
	}

error:
	threadexits(nil);
}

static void
setupsess(Gang *g, Session *s, char *path, int r, int sub)
{
	if(path){
		s->path = smprint("%s/%s/proc", amprefix, path);
	} else {
		s->path = smprint(execfspath);
	}
	DPRINT(DEXE, "setupsess: path=(%s) gangpath=(%s) execfspath=(%s) \n",
	       path, gangpath, execfspath);
	DPRINT(DEXE, "\tresult session path=(%s)\n", s->path);

	s->r = nil;
	s->chan = chancreate(sizeof(char *), 0);
	s->g = g; /* back pointer */

	// remote_override does not work here as r = the number of sessions
	s->remote = r;

	s->subindex = sub; /* keep the subsession index to help with debugging */
}

/* local reservation */
static char *
reslocal(Gang *g)
{
	int count;
	char *err = nil;

	DPRINT(DEXE, "reslocal: size=(%d)\n", g->size);

	if(mystats.next == nil)
		mystats.njobs += g->size;

	dprint_status(&mystats, "reslocal", 0);

	g->sess = emalloc9p(sizeof(Session)*g->size);
	/* TODO: check return value? */
	for(count = 0; count < g->size; count++) {
		setupsess(g, &g->sess[count], nil, 0, count);
		DPRINT(DEXE, "reslocal: reservation: count=%d out of %d\n", count+1, g->size);
		proccreate(cloneproc, &g->sess[count], STACK);
	}

	for(count = 0; count < g->size; count++) {
		char *myerr;
		DPRINT(DEXE, "\twaiting on reservation: count=%d out of %d\n", count+1, g->size);
		myerr = recvp(g->sess[count].chan);
		if(myerr != nil) 
			err = myerr;
			/* MAYBE: retry here? for reliability ? */
	}
	
	return err;
}

/* multinode reservation */
static char *
resgang(Gang *g)
{
	int *njobs; /* array of children with jobs per child */
	int remaining;
	Status *current;
	int scount = 0;
	int count;
	char *err = nil;
	int size = g->size;

	g->size=0;

	/* MAYBE: Lock? */
	/* TODO: implement other modes (block, time-share) */
	
	/* we need to know the current status of the job market... */
	refreshstatus(&mystats);

	DPRINT(DEXE, "resgang: requesting size=%d out of %d=(mystats.ntask=%d) - (mystats.njobs=%d)\n", 
	       size, mystats.ntask-mystats.njobs, mystats.ntask, mystats.njobs);
	if(size > (mystats.ntask-mystats.njobs)) {
		DPRINT(DEXE, "insufficient resources for %d out of %d\n", 
		       size, mystats.ntask-mystats.njobs);
		return estrdup9p(Enores);
	}
	
	/* Lock down gang */
	njobs = emalloc9p(sizeof(int)*mystats.nchild);
	remaining = size;
	/* walk children tree, allocating cores and incrementing stats.njobs
		counting sessions and keeping children per subsession in an array? */
	for(current=mystats.next; current !=nil; current = current->next) {
		dprint_status(current, "resgang (child stats)", 0);

		int avail = current->ntask - current->njobs;
		if(avail >= remaining) {
			njobs[scount] = remaining;
			current->njobs += njobs[scount];
			break;
		} else {
			njobs[scount] = current->ntask - current->njobs;
			remaining -= njobs[scount];
		}
		current->njobs += njobs[scount];
		if(njobs[scount] > 0)	/* only increment when we've allocated a subnode */
			scount++;
	}
	dprint_status(&mystats, "resgang (master stats)", 0);

	scount++; /* count instead of index now */
	
	/* allocate subsessions */
	g->sess = emalloc9p(sizeof(Session)*scount);
	g->size = scount;

	DPRINT(DEXE, "Gang opening up %d sessions\n", scount);

	/* while more to schedule */
	current = mystats.next;
	for(count = 0; count < scount; count++) {
		checkmount(current->name);
		// the child might already be busy, so just skip it for now
		//assert(njobs[count] != 0);
		if(njobs[count] == 0)
			continue;
		DPRINT(DEXE, "resgang: Session %d Target %s Njobs: %d From %s\n",
		       count, current->name, njobs[count], mysysname);
		// EBo -- FIXME -- needs to chec for children or (or leaf)
		setupsess(g, &g->sess[count], current->name, njobs[count], count);
		///// setupsess(g, &g->sess[count], current->name, 0, count);
		proccreate(cloneproc, &g->sess[count], STACK);
		current = current->next;
	}

	DPRINT(DEXE, "waiting on %d sessions\n", scount);

	for(count = 0; count < scount; count++) {
		char *myerr;
		DPRINT(DEXE, "\twaiting on reservation: count=%d out of %d\n", count+1, scount);
		assert(g->sess[count].chan != 0); /* or you are stupid */
		myerr = recvp(g->sess[count].chan);

		if(myerr != nil) {
			DPRINT(DERR, "*ERROR*: session %d broke for (%s) err=(%s)\n", count, g->sess[count].path, myerr); // FIXME: g->path appears to always be nil try session info
			err = myerr;
			g->status = GANG_BROKE;
		}
                /* MAYBE: retry here? for reliability ? */

		/* 
		   This is extremely fragile, and should be looked at
		   to retry, fail over to another session, and other
		   fault tollerance behaviour.  There are a couple of
		   possible scenerios here 1) the node is dead, 2)
		   there is a race condition, ...

		   Plan:
		     * disable node (not gang)
		     * keep a node fail counter
		     * allow possible restart via the status update or
		       retry.

		   For now, just disable and move on.  

		   FIXME: we need to examine/discuss what could be
		   happening with a task whose operation changes the
		   behaviour of the system and if it is possible that
		   failing in the middle of an operation destabalizes
		   the system.

		 */
	}

	return err;
}

/* TODO: many of the error scenarios here probably need more cleanup */
static void
cmdres(Req *r, int num)
{
	Fid *f = r->fid;
	Gang *g = f->aux;

	char dest[255];
	char *mntargs;
	char *err;

	/* okay - by this point we should have gang embedded */
	if(g == nil) {
		respond(r, Enogang);
		return;
	} 

	if(g->size > 0) {
		respond(r, Ebusy);
		return;
	}

	/* setup aggregation I/O */
	snprint(dest, 255, "%s/g%d", gangpath, g->index);

	switch(g->imode) {
		case CMbcast:
			DPRINT(DGAN, "cmdres: broadcast mode\n");
			if (mpipe(dest, "-b stdin") < 0) {
				DPRINT(DGAN, "setting up stdin pipe failed\n");
				respond(r, Estdin);
				return;
			}
			break;
		case CMenum:
			DPRINT(DGAN, "cmdres: enumerated mode\n");
			mntargs = smprint("-e %d stdin", num);
			if (mpipe(dest, mntargs)< 0) {
				free(mntargs);
				respond(r, Estdin);
				return;
			}
			free(mntargs);
			break;
		default:
			respond(r, Ebadimode);
			return;
	};
	if (mpipe(dest, "stdout") < 0) {  
		respond(r, Estdout);
		return;
	}
	if (mpipe(dest, "stderr") < 0) { 
		respond(r, Estderr);
		return;
	}

	g->size = num;

	/* this assumes top down reservation model */
	if(mystats.nchild)
		err = resgang(g);
	else
		err = reslocal(g);

	if(err) {
		DPRINT(DERR, "*ERROR*: problem establishing gang reservation: %s\n", err);
		respond(r, err);
		/* TODO: we have to cleanup gang and sessions here */
		
		free(err);
	} else {
		DPRINT(DEXE, "\t gangs all here\n"); 
		g->status = GANG_RESV;
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	}
}

/* this should probably be a generic function somewhere */
static char *
cleanupcb(char *buf, int count)
{
	int c;
	char *nbuf;
	/* check last few bytes for crap */
	for(c = count-1; c < count-3; c--) {
		if((buf[c] < ' ')||(c>'~'))
			buf[c] = 0;
	}
	
	nbuf = emalloc9p(count+1);
	memmove(nbuf, buf, count);
	nbuf[count] = 0;
	return nbuf;
}

static void
relaycmd(void *arg)
{
	Session *s = arg;
	int n;

	DPRINT(DEXE, "\trelaycmd: writing %d count of data to %d\n", s->r->ifcall.count, s->fd);
	n = pwrite(s->fd, s->r->ifcall.data, s->r->ifcall.count, 0);
	if(n < 0) {
		sendp(s->chan, smprint("*ERROR*: relaycmd: write cmd failed: %r\n"));
	} else {
		sendp(s->chan, nil);
	}
}

static void
cmdbcast(Req *r, Gang *g)
{
	int count;
	char *resp;
	char *err = nil;

	char *cmd = estrdup9p(r->ifcall.data);
	if(cmd[r->ifcall.count-1]=='\n')
		cmd[r->ifcall.count-1] = 0;
	else
		cmd[r->ifcall.count] = 0;

	DPRINT(DBCA, "cmdbcast: broadcasting (%s) to gang %d (size: %d)\n", cmd, g->index, g->size);
	free(cmd);

	/* broadcast to all subsessions */
	for(count = 0; count < g->size; count++) {
		/* slightly slimy */
		g->sess[count].r = r;
		proccreate(relaycmd, &g->sess[count], STACK);
		DPRINT(DEXE, "\t**** cmdbcast[%d]: relaycmd created\n", count);
	}
	
	DPRINT(DEXE, "\twaiting for responses\n");
	/* wait for responses, checking for errors */
	for(count = 0; count < g->size; count++) {
		DPRINT(DEXE, "\t\twaiting on relay[%d]\n", count);
		/* wait for response */
		resp = recvp(g->sess[count].chan);
		// FIXME: do we need to free anything up?
		DPRINT(DEXE, "\t\treturning err=(%s)\n", resp);
		g->sess[count].r = nil;
		if(resp)
			err = resp;
	}
	
	if(err) {
		DPRINT(DEXE, "cmdbcast: returning err=(%s)\n", err);
		respond(r, err);
	} else {
		/* success */
		DPRINT(DEXE, "cmdbcast: returning success\n");
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	}
}

static void
fswrite(void *arg)
{
	Req *r = (Req *)arg;
	Fid *f = r->fid;
	Qid *q = &f->qid;
	Gang *g = f->aux;
	char *err;
	char e[ERRMAX];
	char *buf;
	Cmdbuf *cb;
	Cmdtab *cmd;
	int num;
	
	switch(TYPE(*q)) {
	default:
		snprint(e, sizeof e, "bug in gangfs path=%llux\n", q->path);
		respond(r, e);
		break;
	case Qgstat:
		buf = estrdup9p(r->ifcall.data);
		buf[r->ifcall.count] = 0;
		if(buf == nil) {
			respond(r, Ebadstat);
			return;
		}
		if(r->ifcall.count < 20) {
			DPRINT(DCUR, "weird status write: size: %d buf: %p\n", r->ifcall.count, buf);
			respond(r, Ebadstat);
			free(buf);
			return;	
		}
		DPRINT(DCUR, "fswrite(Qgstat): statuswrite buf: %s\n", buf);
		err = statuswrite(buf);	/* TODO: add error checking & reporting */
		free(buf);
		if(err) {
			respond(r, err);
		} else {	
				r->ofcall.count = r->ifcall.count;
				respond(r, nil);
		}		
		return;
	case Qctl:
		if(r->ifcall.count >= 1024) {
			respond(r, Etolong);
			return;
		}
		if(g->status > GANG_EXEC) {
			respond(r, Eclosing);
			return;
		}	
		
		buf = cleanupcb(r->ifcall.data, r->ifcall.count);
		cb = parsecmd(buf, strlen(buf));
		cmd = lookupcmd(cb, ctlcmdtab, nelem(ctlcmdtab));
		if(cmd == nil) {
			if((g == nil)||(g->size == 0)) {
				respondcmderror(r, cb, "%r");
			} else {
				DPRINT(DEXE, "Unknown command, broadcasting to children\n");
				cmdbcast(r, g);

//gangrefdec(g, "testing cmdbcast:");
				// FIXME: on large numbers of tasks,
				// some of the stdouts appear to be
				// removed before they are fully
				// processed.

				//flushgang(g);
				//sleep(5000);

				// FIXME: I am not sure this is the
				// most appropriate place to put this...
				//DPRINT(DCUR, "******** releasegang\n");
				//releasegang(g);
//				DPRINT(DCUR, "******** cleanupgang\n");
//				cleanupgang(g);
//				DPRINT(DCUR, "******** done\n");
//DPRINT(DCUR, "hacking the refcount\n");
//gangrefdec(g, "cmdbcast");
//gangrefdec(g, "cmdbcast");
//gangrefdec(g, "cmdbcast");
//printganglist();
			}
		} else {
			switch(cmd->index) {
			default:
				respondcmderror(r, cb, "unimplemented");
				break;
			case CMres:	/* reservation */	
				if(cb->nf < 2) {
					respondcmderror(r, cb, "res insufficient args %d", cb->nf);
					break;
				}
				num = atol(cb->f[1]);
				DPRINT(DGAN, "reserve num=(%d)\n", num);
				if(num < 0) {
					respondcmderror(r, cb, "bad arguments: %d", num);
				} else {
					cmdres(r, num);
				}
				break;
			case CMmount: /* check mount */
				DPRINT(DGAN, "check mount %s (%d)\n", cb->f[1], cb->nf);
				if(checkmount(cb->f[1]) < 0) {
					respond(r, Echeckmount);
				} else {
					r->ofcall.count = r->ifcall.count;
					respond(r, nil);
				}
				break;				
			case CMenum: /* enable enumeration mode */
				DPRINT(DGAN, "Setting enumerated mode\n");
				g->imode = CMenum;
				r->ofcall.count = r->ifcall.count;
				respond(r, nil);
				break;
			case CMbcast: /* enable broadcast mode */
				DPRINT(DGAN, "Setting broadcast mode\n");
				g->imode = CMbcast;
				r->ofcall.count = r->ifcall.count;
				respond(r, nil);
				break;		
			};
		}
		free(cb);
		free(buf);
		break;		
	};
}

static void
fsstat(Req* r)
{
	Fid *fid = r->fid;
	int n = TYPE(fid->qid) - 1;

	if(TYPE(fid->qid) >= Qsubses) {
		respond(r, Eperm);
		return;
	}

	if (dirgen(n, &r->d, fid) < 0)
		respond(r, Enotfound);
	else
		respond(r, nil);
}

static void
fsclunk(Fid *fid)
{
	Gang *g = fid->aux;

	DPRINT(DFID, "fsclunk: %d\n", fid->fid);
	if(TYPE(fid->qid) == Qctl) {
		DPRINT(DFID, "\tand its a ctl\n");
		if(g) {
			wlock(&glock);
			g->ctlref--;	
			DPRINT(DREF, "\tctlref=%d\n", g->ctlref);
			if(g->ctlref == 0) {
				if(mystats.next == nil)
					mystats.njobs -= g->size;
				g->status=GANG_CLOSING;
				wunlock(&glock);
				DPRINT(DFID, "fsclunk: g->status=GANG_CLOSING for gang %p path=%s\n", g, g->path);
				proccreate(cleanupgang, (void *)g, STACK); 
			} else {
				wunlock(&glock);
			}
		} else
			// FIXME: check on this...  The first time through g is nil
			DPRINT(DERR, "*WARNING*: fsclunk: ctl fid %d without aux\n", fid->fid);
	} else {
		if(g) {
			DPRINT(DFID, "fsclunk: releasing gang %p path=%s\n", g, g->path);
			releasegang(g);
		}
	}
}

/* handle certain ops in a separate thread in original namespace */
static void
iothread(void*)
{
	Req *r;

	threadsetname("gangfs-iothread");
	if(sendp(iochan, nil) < 0) {
		fprint(2, "iothread: problem sending sync message\n");
		threadexits("nosync");
	}

	for(;;) {
		r = recvp(iochan);
		if(r == nil)
			threadexits("interrupted");

		switch(r->ifcall.type){
		case Topen:
			fsopen(r);	
			break;
		case Tread:
			fsread(r);			
			break;
		case Twrite:
			proccreate(fswrite, (void *) r, STACK);
			break;
		case Tclunk:
			fsclunk(r->fid);
			sendp(clunkchan, r);
			free(r);
			break;
		default:
			fprint(2, "unrecognized io op %d\n", r->ifcall.type);
			break;
		}
	}
}

static void
ioproxy(Req *r)
{
	if(sendp(iochan, r) != 1) {
		fprint(2, "iochan hungup");
		threadexits("iochan hungup");
	}
}

static void
clunkproxy(Fid *f)
{
	/* really freaking clunky, but not sure what else to do */
	Req *r = emalloc9p(sizeof(Req));

	DPRINT(DFID, "clunkproxy: %p fidnum=%d aux=%p pid=%d\n", 
	       f, f->fid, f->aux, getpid());

	r->ifcall.type = Tclunk;
	r->fid = f;
	if(sendp(iochan, r) != 1) {
		fprint(2, "iochan hungup");
		threadexits("iochan hungup");
	}
	DPRINT(DFID, "clunkproxy: waiting on response\n");
	recvp(clunkchan);
}


Srv fs=
{
	.attach=	fsattach,
	.walk1=		fswalk1,
	.clone=		fsclone,
	.open=		ioproxy,
	.write=		ioproxy,
	.read=		ioproxy,
	.stat=		fsstat,
	.destroyfid=clunkproxy,
	.end=		cleanup,
};

void
threadmain(int argc, char **argv)
{
	char *x;
	char *parent = nil;

	updateinterval = 5;	/* 5 second default update interval */
	mysysname = getenv("sysname");

	srvpath    = defaultsrvpath;
	srvmpipe   = defaultsrvmpipe;
	procpath   = defaultpath;
	execfspath = defaultpath;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'v':
		x = ARGF();
		if(x)
			vflag = atol(x);
		break;
	case 'p':	/* specify parent */
		parent = ARGF();
		break;
	case 's':	/* specify server name */
		srvpath = ARGF();
		break;
	case 'n':	/* name override */
		mysysname = estrdup9p(ARGF());
		break;
	case 'm':	/* mntpt override */
		procpath = ARGF();
		break;
	case 'E':	/* mntpt override */
		execfspath = ARGF();
		break;
	case 'i':	/* update interval (seconds) */
		x = ARGF();
		if(x)
			updateinterval = atoi(x);
		break;
	case 'R':
		remote_override = 1;
		remote = atoi(ARGF());
		break;
	case 'M':
		srvmpipe = ARGF();
		break;
	case 'N':
		ntask_override = atoi(ARGF());
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	gangpath = procpath;
	xgangpath = procpath; /* until we get a statuswrite */

	if(parent) {
		xgangpath = smprint("%s/%s%s", amprefix, parent, procpath);
		DPRINT(DFID, "Main: parent setting xgangpath = (%s)\n", xgangpath);
	}

	if(remote_override) {
		// FIXME: extra proc?  from procpath, and added in by had...
		xgangpath = smprint("%s/%s%s", amprefix, mysysname, procpath);
		DPRINT(DFID, "Main: xgangpath overide = (%s)\n", xgangpath);
	}

	/* initialize status */
	initstatus(&mystats);
	
	DPRINT(DFID, "Main: procpath=(%s) pid=(%d)\n", procpath, getpid());
	DPRINT(DFID, "\tsrvpath=(%s) procpath=(%s) mysysname=(%s)\n", 
	       srvpath, procpath, mysysname);
	DPRINT(DFID, "\tsrvmpipe=(%s)\n", srvmpipe);

	if(parent) {
		DPRINT(DFID, "      has parent=(%s)\n", parent);
		if(checkmount(parent) < 0) {
			DPRINT(DERR, "*ERROR*: Connecting to parent failed\n");
			threadexits("problem connecting to parent\n");
		}
		proccreate(updatestatus, (void *)parent, STACK);
	}

	/* spawn off a io thread */
	iochan = chancreate(sizeof(void *), 0);
	clunkchan = chancreate(sizeof(void *), 0);
	iothread_id = proccreate(iothread, nil, STACK);
	recvp(iochan);

	DPRINT(DFID, "Main: iothread_id=(%d) mysysname=(%s)\n",
	       threadpid(iothread_id), mysysname);

	threadpostmountsrv(&fs, srvpath, procpath, MAFTER);

	threadexits(nil);
}
