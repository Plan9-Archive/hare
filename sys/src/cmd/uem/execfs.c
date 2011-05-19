/*
	execfs - proc file system execution wrapper

	Copyright (C) 2010, IBM Corporation, 
 		Eric Van Hensbergen (bergevan@us.ibm.com)

	Description:
		This provides an overlay file system which provides
		facilities for process creation using a clone mechanism.

		Along with that, we create stdio pipes for the process
		to use as I/O, and overlay those on top of the created pid
		in the proc table.

	TODO:
		* maybe autostart mpipe when we need it (on-demand pipes)
		
*/

#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <mp.h>
#include <libsec.h>
#include <stdio.h>
#include <debug.h>

static char Enopid[] =	"process not initialized";
static char Eoverflow[] = "ctl buffer overflow";
static char Ebadctl[] = "bad ctl message";
static char Ebadfid[] = "bad fid";
static char Eexec[] = "could not exec";
static char Eusage[] = "exec: invalid number of arguments";
static char Ectlchan[] ="problems with command channel to wrapper";
static char Ectlopen[] ="problems with opening command channel to wrapper";
static char Empipe[] = "problems with mpipe";
static char Epid[] = "stupid pid problem";
static char Esrv[] = "couldn't create srv file";
static char Ewtf[] = "I have no idea what could be going wrong here";

enum {	/* DEBUG LEVELS */
	DERR = 0,	/* error */
	DCUR = 1,	/* current - temporary trace */
	DFID = 9,	/* fid tracking */
	DARG = 10,	/* arguments */
};

QLock lck;
int num_mnts = 0;
int *mnts = nil;
int iothread_id;

static char defaultpath[] =	"/proc";
static char defaultsrvpath[] =	"execfs";
static char *procpath, *srvpath;
static char *srvctl;
static Channel *iochan;
static Channel *clunkchan;

static void
usage(void)
{
	fprint(2, "execfs [-D] [-v debuglevel] [-m mtpt]\n");
	exits("usage");
}

enum
{
	STACK = (8 * 1024),
	STRMAX = 255,
	Xclone = 1,
};

typedef struct Exec Exec;
struct Exec 
{
	int active;	/* setup=0, executing=1 */
	int pid;	/* maybe this would be sufficient for above? */
	int ctlfd;	/* command and control channel */
	int rctlfd;	/* actual control channel */
};

enum
{
	Cexec,
};

Cmdtab ctltab[]={
	Cexec,	"exec", 0,
};

static int
mpipe(char *path, char *name)
{
	int fd, ret;
	fd = open("/srv/mpipe", ORDWR);
	if(fd<0) {
		DPRINT(DERR, "*ERROR*: couldn't open /srv/mpipe: %r\n");
		return -1;
	}
	
	DPRINT(DFID, "mpipe: mounting (%s) on (%s) pid=(%d)\n",
	       name, path, getpid());
	ret = mount(fd, -1, path, MAFTER, name);
	if(ret<0)
		DPRINT(DERR, "*ERROR*: mpipe mount failed: %r\n");

	close(fd);
	return ret;
}

/* FIXME: debugging... */
static void
lsproc(void *arg)
{
	Channel *pidc = arg;

	threadsetname("execfs-lsproc");
	DPRINT(DFID, "lsproc (%p) (%s) (%s) (%s) (%s) pid=(%d): %r\n",
	       pidc, "/bin/ls", "ls", "-l", procpath, getpid());
	procexecl(pidc, "/bin/ls", "ls", procpath, nil);

	sendul(pidc, 0);
	threadexits(nil);

}

/* call the execution wrapper */
static void
cloneproc(void *arg)
{
	Channel *pidc = arg;

	threadsetname("execfs-cloneproc");

	DPRINT(DFID, "cloneproc pidc=(%p) (%s) (%s) (%s) (%s) (%s) (%s) pid=(%d): %r\n",
	       pidc, "/bin/execcmd", "execcmd", "-s", srvctl,
	       "-v", smprint("%d", vflag), getpid());
	procexecl(pidc, "/bin/execcmd", "execcmd", "-s", srvctl, "-v", 
		  smprint("%d", vflag), nil);

	sendul(pidc, 0); /* failure */
	threadexits(nil);
}

/* separated process creation to a function to help with debug */
static ulong
kickit(void)
{
	Channel *pidc = chancreate(sizeof(ulong), 0);
	ulong pid;

	DPRINT(DCUR, "kickit: forking the proc\n");
	int npid = procrfork(cloneproc, (void *) pidc, STACK, RFFDG);
	DPRINT(DCUR, "\tnpid=(%d)\n", npid);
	pid = recvul(pidc);
	if(pid==0) {
		DPRINT(DERR, "*ERROR*: Problem with cloneproc\n");
	}
/* FIXME: debuging... the proc still is there...
procrfork(lsproc, (void *) pidc, STACK, RFFDG);
ulong lspid = recvul(pidc);
if(lspid==0) {
	DPRINT(DERR, "*ERROR*: Problem with cloneproc\n");
}
*/

	chanfree(pidc);

	return pid;
}

static void
fsopen(Req *r)
{
	int n;
	int p[2];
	int fd;
	Exec *e;
	char *err;
	Fid *f = r->fid;
	char *fname = (char *) emalloc9p(STRMAX);	/* pathname buffer */
	char *ctlbuf = (char *) emalloc9p(STRMAX);	/* error string from wrapper */

	if(f->file->aux != (void *)Xclone) {
		respond(r, nil);
		return;
	}

	e = emalloc9p(sizeof(Exec));
	memset(e, 0, sizeof(Exec));

	/* setup bootstrap ctlfd */
	DPRINT(DFID, "fsopen srvctl=(%s) pid=(%d)\n", srvctl, getpid());

	pipe(p);
	qlock(&lck);
	fd = create(srvctl, OWRITE, 0666);
	qunlock(&lck);
	if(fd < 0) {
		DPRINT(DFID, "fsopen failed for srvctl=(%s): %r\n", srvctl);
		err = estrdup9p(Esrv);
		goto error;
	}

	fprint(fd, "%d", p[0]);
	close(fd);
	close(p[0]);
	e->ctlfd = p[1];
     
	/* ask for a new child process */
	e->pid = (int) kickit();

	/* cloneproc generates stdin, stdout, and stderr for the ctl
	 * channel.  If any of these fail, then e->pid will be < 2, and
	 * this is a hard failure, so crash instead of dieing
	 * gracefully */
	DPRINT(DFID, "number of new pids=(%d): %r\n", e->pid);
	assert(e->pid > 2);	/* assumption for our ctl channels */

	/* grab actual reference to real control channel */
	n = snprint(fname, STRMAX, "%s/%d/ctl", procpath, e->pid);
	assert(n > 0);
	
	/* grab ahold of the ctl file */
	if((e->rctlfd = open(fname, OWRITE)) < 0) {
		DPRINT(DERR, "*ERROR*: opening %s failed: %r\n", fname);
		err = smprint("execfs: fsopen: opening [%s] failed: %r", fname);
		goto error;
	}

	n = snprint(fname, STRMAX, "%s/%d", procpath, e->pid);
	assert(n > 0);

	/* asserts are heavy handed, but help with debug */
	n = mpipe(fname, "stdin");
	assert(n > 0);
	n = mpipe(fname, "stdout");
	assert(n > 0);
	n = mpipe(fname, "stderr");
	assert(n > 0);

	// FIXME: hack to keep track of mnts
	qlock(&lck);
	mnts = erealloc9p(mnts, (num_mnts+1)*sizeof(int));
	mnts[num_mnts++] = e->pid;
	qunlock(&lck);
	DPRINT(DCUR, "fsopen: mounted new pipe num_mnts=(%d)\n", num_mnts);

	/* handshake to execcmd */
	n = write(e->ctlfd, "1", 1);
	if(n < 0) {
		err = estrdup9p(Ectlchan);
		goto error;	
	}

	/* check for partial success: execcmd has opened stdio */
	n = read(e->ctlfd, ctlbuf, STRMAX);
	if(n < 0) {
		err = estrdup9p(Ectlchan);
		goto error;	
	}
	if(n != 1) {
		ctlbuf[n] = 0;
		err = estrdup9p(ctlbuf);
		goto error;
	}

	f->aux = e;
	free(fname);
	free(ctlbuf);
	respond(r, nil);
	return;

error:
	free(fname);
	free(ctlbuf);
	/* free channel */
	free(e);
	f->aux = nil;
	respond(r, err);
	free(err);
}

static void
fswrite(Req *r)
{
	Fid *f = r->fid;
	Exec *e = f->aux;
	char ctlbuf[256];
	int ret;
	
	if(e == 0) {
		respond(r, Ebadfid);
	}
	assert(e->ctlfd != -1);

	DPRINT(DFID, "fswrite: ctlfd=(%d) count=(%d) pid=(%d)\n",
	       e->ctlfd, r->ifcall.count, getpid());

	ret = write(e->ctlfd, r->ifcall.data, r->ifcall.count);
	if(ret < 0) {
		DPRINT(DFID, "*ERROR*(fswrite): write pid=(%d): %r\n", getpid());
		responderror(r);
		return;
	} else if(e->active) {	/* program already executing */
		goto success;
	}

	/* not active yet: read to get response */

	/* this assumes that the only thing expected to be written is
	 * the exec cmd if the session is not yet active. */
	ret = read(e->ctlfd, ctlbuf, STRMAX);
	if(ret < 0) {
		DPRINT(DFID, "*ERROR*(fswrite): read pid=(%d): %r\n", getpid());
		responderror(r);
		return;
	}
	
	if(ret == 0) {		/* other side exec'd - go active */
		close(e->ctlfd);
		/* replace our channel with the real control channel */
		e->ctlfd = e->rctlfd;
		e->active++;
		DPRINT(DFID, "fswrite: go active=(%d)\n", e->active);

		goto success;
	}

	/* convention: a single byte is sucess, multibyte is an error message */
	if(ret != 1) {
		DPRINT(DFID, "fswrite: single byte success\n");
		ctlbuf[ret] = 0;
		respond(r, ctlbuf);
		return;
	}
success:
	DPRINT(DFID, "fswrite: generall success\n");
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
	return;
}

static void
fsread(Req *r)
{
	Fid *f = r->fid;
	Exec *e = f->aux;

	DPRINT(DFID, "fsread: ctlfd=(%d) count=(%d) pid=(%d)\n",
	       e->ctlfd, r->ifcall.count, getpid());

	if(e->pid == 0) {
		DPRINT(DERR, "*ERROR*(fsread): no pid\n");
		respond(r, Enopid);
	} else {
		DPRINT(DFID, "fsread: reading...\n");
		char buf[16];
		snprint(buf, 16, "%d", e->pid);
		readstr(r, buf);
		respond(r, nil);
	}	
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
		DPRINT(DERR, "flushmp: open failed: %r\n");
		return fd;
	}

	n = snprint(hdr, 255, "%c\n%lud\n%lud\n%s\n",
		    pkttype, (ulong)0, (ulong)0, "");
	DPRINT(DCUR, "flushmp: src=(%s) pid=(%d)\n", src, getpid());

	n = pwrite(fd, hdr, n, tag);
	close(fd);
	return n;
}

static void
cleanupsession(void *arg)
{
	int pid = (int) arg;
	char *path = (char *) emalloc9p(STRMAX);

	DPRINT(DFID, "cleanupsession: pid=(%d)\n", getpid());

	/* BUG: what if we can't get to them because proc is already
	 * closed? */
	snprint(path, STRMAX, "%s/%d", procpath, pid);

/*
	DPRINT(DFID, "cleanupsession: unmounting and killing %s pid=(%d)\n", path, pid);
	ret = unmount(0, path);
	if(ret == -1) {
		DPRINT(DFID, "  ERROR: unmounting (%d) failed\n", pid);
	}
	ret = postnote(PNPROC, pid, "kill");
	if(ret == -1) {
		DPRINT(DFID, "  ERROR: postnote (%d) failed\n", pid);
	}
*/
	free(path);
}

static void
fsclunk(Fid *f)
{
	DPRINT(DFID, "fsclunk: %p fid=%d aux=%p\n", f, f->fid, f->aux);
	if(f->aux) {
		Exec *e = f->aux;

		DPRINT(DFID, "Should be cleaning up\n");
		if(e->ctlfd) {
			DPRINT(DFID, "    closing fd=(%d)\n", e->ctlfd);
			close(e->ctlfd);
		}

		proccreate(cleanupsession, (void *)e->pid, STACK);

		e->pid = -1;
		e->active = 0;

		free(e);
		f->aux = nil;
	}
}

static void
cleanup(Srv *)
{
	DPRINT(DCUR, "cleanup: pid=(%d)\n", getpid());
	nbsendp(iochan, 0); /* flush any blocked readers */

	DPRINT(DCUR, "cleanup: (%d) mounted. unmounting...\n", num_mnts);
	char *fname = (char *) emalloc9p(STRMAX);
	flushmp("/n/mpipetest"); /* flush pipe to be sure */

/*** FIXME: already removed when the execcmd dies
	int i, ret;
	for(i=num_mnts-1; i>=0; i--) {
		snprint(fname, STRMAX, "%s/%d", procpath, mnts[i]);
		DPRINT(DFID, "\tfname=(%s)\n", fname);
		
		flushmp(fname); 

		ret = unmount(fname, "stdin");
		if(ret)
			DPRINT(DFID, "\tFAILED unmounting fname=(%s)\n", fname);
		unmount(fname, "stout");
		unmount(fname, "sterr");
	}
***/
	free(fname);


	DPRINT(DFID, "freeing the server, io and clunk channel\n");
	remove(srvctl);
	chanfree(iochan);
	chanfree(clunkchan);
	free(srvctl);

	//DPRINT(DFID, "cleanup: iothread's pid=(%d)\n", threadpid(iothread_id));
	//threadkill(iothread_id);

	threadexitsall("done");
}

/* handle certain ops in a separate thread in original namespace */
static void
iothread(void*)
{
	Req *r;

	threadsetname("execfs-iothread");
	for(;;) {
		r = recvp(iochan);
		if(r == nil)
			threadexits("interrupted");

		switch(r->ifcall.type){
		case Topen:
			fsopen(r);
			break;
		case Twrite:
			fswrite(r);
			break;
		case Tclunk:
			fsclunk(r->fid);
			sendp(clunkchan, r);
			break;
		default:
			DPRINT(DERR, "*ERROR*: unrecognized io op %d\n", r->ifcall.type);
			break;
		}
	}
}

static void
ioproxy(Req *r)
{
	DPRINT(DFID, "ioproxy...\n");

	if(sendp(iochan, r) != 1) {
		DPRINT(DFID, "ioproxy: iochan hungup...\n");
		fprint(2, "iochan hungup");
		threadexits("iochan hungup");
	}
}

static void
clunkproxy(Fid *f)
{
	/* really freaking clunky, but not sure what else to do */
	Req *r = emalloc9p(sizeof(Req));

	DPRINT(DFID, "clunkproxy: %p fidnum=%d aux=%p\n", f, f->fid, f->aux);

	r->ifcall.type = Tclunk;
	r->fid = f;
	if(sendp(iochan, r) != 1) {
		fprint(2, "iochan hungup");
		threadexits("iochan hungup");
	}
	recvp(clunkchan);

	free(r);
}

Srv fs=
{
	.open=		ioproxy,
	.write=		ioproxy,
	.read=		fsread,
	.destroyfid=	clunkproxy,
	.end=		cleanup,
};

void
threadmain(int argc, char **argv)
{
	char *x;

	//srvpath = defaultsrvpath;
	srvpath = nil;

	procpath = defaultpath;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'm':	/* mntpt override */
		procpath = ARGF();
		break;
	case 'v':
		x = ARGF();
		if(x)
			vflag = atoi(x);
		break;		
	case 's':	/* specify server name */
		srvpath = ARGF();
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	srvctl = smprint("/srv/execfs-%d", getpid());

	fs.tree = alloctree("execfs", "execfs", DMDIR|0555, nil);
	closefile(createfile(fs.tree->root, "clone", "execfs", 0666, (void *)Xclone));

	/* spawn off a io thread */
	iochan = chancreate(sizeof(void *), 0);
	clunkchan = chancreate(sizeof(void *), 0);

	iothread_id = proccreate(iothread, nil, STACK);
	DPRINT(DFID, "Main: srvpath=(%s) procpath=(%s)\n",
	       srvpath, procpath);
	DPRINT(DFID, "Main: main pid=(%d) iothread=(%d)\n",
	       getpid(), threadpid(iothread_id));

	threadpostmountsrv(&fs, srvpath, procpath, MAFTER);
	threadexits(0);
}
