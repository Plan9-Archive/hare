/*
	gang2fs - aggregated execution management (take 2)

	Copyright (C) 2010,2011 IBM Corporation 
 		Eric Van Hensbergen (bergevan@us.ibm.com)

	Description:
		This provides a file system which interacts with execfs(4)
		to provide a gang execution environment.  This initial version
		will be single-node, but the intention is to support multinode
		environments

	Based in part on Pravin Shinde's devtask.c

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
#include "../debug.h"
#include "gangfs.h"

enum
{
	STACK = (8 * 1024),
	NAMELEN = 255,
	NUMSIZE = 12,
};

static char defaultsrvpath[] =	"gangfs";
static char defaultsrvmpipe[] =	"mpipe";
static char *srvpath, *srvmpipe;
int iothread_id;

/* 
  qid.path format:
	[MAGIC(16)][CONV(16)][TYPE(16)] where low 00ff bytes are type
*/
#define MAGIC		 ((uvlong)0x6AF5<<32)
#define TYPE(x) 	 ((ulong) (x).path & 0xff)
#define CONVP(x) 	 ((((ulong) (x).path >> 16)&0xffff) -1)
#define CONV(x) 	 ((((ulong) (x)->path >> 16)&0xffff) -1)
#define CONVQIDP(c, y) ((uvlong)(MAGIC|((((c+1) << 16)|(y)))))

/* Conversation Structure */
typedef struct Conv Conv;
struct Conv
{
	RWLock l;		/* lock protecting the conversation (necessary?) */

	Conv *next;		/* it's a linked list */

	void *priv;		/* private data */

	int refcount;
	int index;
	int size;
	Conv *children;	/* sub-conversations */
}

newconv
delconv
findconv
acquireconv
releaseconv

/* Main conversation list and a lock protecting it */
RWLock clock; 
Conv *clist;

/* member functions for convs
		new - setup a new gang
		del - delete a gang
		findconv - look up gang by number (do we just need an iterator?)DFF
		findconvnum - lookup gang by enumeration
		acquiregang - (new) grab a reference to a gang 
		releasegang - release a reference to the gang
*/

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

	srvpath    = defaultsrvpath;
	srvmpipe   = defaultsrvmpipe;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'v':
		x = ARGF();
		if(x)
			vflag = atoi(x);
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
