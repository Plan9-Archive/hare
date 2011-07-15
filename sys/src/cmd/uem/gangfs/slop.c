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

/* FIXME: debugging... (keep it here? */
static void
lsproc(void *arg)
{
	threadsetname("gangfs-lsproc");
	DPRINT(DFID, "lsproc (%s) (%s) (%s) (%s) pid=(%d)\n",
	       "/bin/ls", "ls", "-l", arg, getpid());
	procexecl(nil, "/bin/ls", "ls", "-l", arg, nil);

	threadexits(nil);

}

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

	if(parent) {
		DPRINT(DFID, "      has parent=(%s)\n", parent);
		if(checkmount(parent) < 0) {
			DPRINT(DERR, "*ERROR*: Connecting to parent failed\n");
			threadexits("problem connecting to parent\n");
		}
		proccreate(updatestatus, (void *)parent, STACK);
	}

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
	
	DPRINT(DFID, "Main: procpath=(%s) pid=(%d)\n", procpath, getpid());
	DPRINT(DFID, "\tsrvpath=(%s) procpath=(%s) mysysname=(%s)\n", 
	       srvpath, procpath, mysysname);
	DPRINT(DFID, "\tsrvmpipe=(%s)\n", srvmpipe);