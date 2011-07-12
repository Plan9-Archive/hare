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
		DPRINT(DCUR, "updatestatus: parent=(%s) parentpath=(%s)\n", parent, parentpath);
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