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

	/* Clean up subsessions first so they flush data to us */
	releasesessions(g);

	/* make sure that the std files are completely flushed */
	flushgang(arg);

	/* unmount the std files and remove their references */
	DPRINT(DCLN, "cleanupgang: unmounting stdin, stdout, and stderr\n");
	snprint(fname, 255, "%s/g%d/stdin", gangpath, g->index);
	if(unmount(0, fname) == -1)
		DPRINT(DERR, "cleanupgang: *ERROR* unable to unmount %s: %r\n", fname);

	snprint(fname, 255, "%s/g%d/stdout", gangpath, g->index);
	if(unmount(0, fname) == -1)
		DPRINT(DERR, "cleanupgang: *ERROR* unable to unmount %s: %r\n", fname);

	snprint(fname, 255, "%s/g%d/stderr", gangpath, g->index);
	if(unmount(0, fname) == -1)
		DPRINT(DERR, "cleanupgang: *ERROR* unable to unmount %s: %r\n", fname);

	/* close it out */
	g->status=GANG_CLOSED;
	/* now what? */
	
printgang(g);
}


static void
releasegang(Gang *g)
{
	wlock(&glock);
	DPRINT(DEXE, "releasegang:\n");
	
//need to decrement the ref counter here
	gangrefdec(g, "releasegang");
//	if((g->ctlref == 0)&&(g->refcount == 0)) {	/* clean up */
	if(g->ctlref == 0) {	/* clean up */
		Gang *c;
		Gang *p = glist;

		if(g->refcount != 0)
			DPRINT(DCUR, "*WARNING*: releasegang, ctlrefcount=0, gang refcount=%d\n", g->refcount);

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
			DPRINT(DEXE, "setting up backmount failed: %r\n");
			chanprint(sess->chan, "setting up backmount failed: %r\n");
			goto error;
		}

		if(sess->g->imode == CMenum) {
			n = fprint(sess->fd, "enum");
			if(n < 0) {
				DPRINT(DEXE, "setting up enum failed: %r\n");
				chanprint(sess->chan, "setting up enum failed: %r\n");
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