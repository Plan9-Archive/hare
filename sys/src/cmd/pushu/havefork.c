#include "rc.h"
#include "getflags.h"
#include "exec.h"
#include "io.h"
#include "fns.h"

int havefork = 1;
int pids[8192];
int pidp = 0;
int pipep = 0;
extern int npipe;

#define	pushmp(x) ((pipep!=npipe || morempipes()), mpstk[pipep] = (x), pipep++)

pipes*
popmp(void)
{
	if(pipep==0)
		return nil;
	return mpstk[--pipep];
}

int
morempipes(void)
{
	npipe+=10;
	mpstk = (pipes **)realloc((char *)mpstk, npipe*sizeof mpstk[0]);
	if(mpstk==0)
		panic("Can't realloc %d bytes in morempipes!",
				npipe*sizeof mpstk[0]);
	return 0;
}

void
Xasync(void)
{
	int null = open("/dev/null", 0);
	int pid;
	char npid[10];
	if(null<0){
		Xerror("Can't open /dev/null\n");
		return;
	}
	switch(pid = rfork(RFFDG|RFPROC|RFNOTEG)){
	case -1:
		close(null);
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		pushredir(ROPEN, null, 0);
		start(runq->code, runq->pc+1, runq->local);
		runq->ret = 0;
		break;
	default:
		addwaitpid(pid);
		close(null);
		runq->pc = runq->code[runq->pc].i;
		inttoascii(npid, pid);
		setvar("apid", newword(npid, (word *)0));
		break;
	}
}

void
Xpipe(void)
{
	struct thread *p = runq;
	int pc = p->pc, forkid;
	int lfd = p->code[pc++].i;
	int rfd = p->code[pc++].i;
	int pfd[2];
	if(pipe(pfd)<0){
		Xerror("can't get pipe");
		return;
	}
	switch(forkid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(p->code, pc+2, runq->local);
		runq->ret = 0;
		close(pfd[PRD]);
		pushredir(ROPEN, pfd[PWR], lfd);
		break;
	default:
		addwaitpid(forkid);
		start(p->code, p->code[pc].i, runq->local);
		close(pfd[PWR]);
		pushredir(ROPEN, pfd[PRD], rfd);
		p->pc = p->code[pc+1].i;
		p->pid = forkid;
		break;
	}
}

void
Xfanin(void)
{
	int i, j;
	char s[40];
	struct thread *p = runq;
	int pc = p->pc, forkid;
	int lfd = p->code[pc++].i;
	int nfd = p->code[pc++].i;
	int pfd[2];
	
	static int ntimes;
	nfd = 3; // XXX: to test
	pipes *m = emalloc(sizeof(pipes)+nfd * sizeof(int) * 2);
	m->npipe = nfd;
	for(i = 0; i < m->npipe; i++)
		for(j = 0; j < 2; j++)
			if(pipe(m->fd[i][j])<0){
				Xerror("can't get pipe");
				return;
			}	
	pushmp(m);
	if(pipe(pfd)<0){
		Xerror("can't get pipe");
		return;
	}
	// IRF
	switch(forkid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		pushlist();
		for(i = 0; i < m->npipe; i++){
			close(m->fd[i][1][PWR]);
			close(m->fd[i][0][PRD]);
			close(m->fd[i][0][PWR]);
			pushword(smprint("%d", m->fd[i][1][PRD]));
		}
		startargv(p->code, pc+3, runq->local, runq->argv);
		runq->ret = 0;
		close(pfd[PRD]);
		close(0); // irf doesn't need a stdin. 
		pushredir(ROPEN, pfd[PWR], 1);
		return;
	default:
		addwaitpid(forkid);
		break;
	}
	switch(forkid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(p->code, p->code[pc+1].i, runq->local);
		for(i = 0; i < m->npipe; i++){
			for(j = 0; j < 2; j++){
				close(m->fd[i][j][PRD]);
				close(m->fd[i][j][PWR]);
			}
		} 
		close(pfd[PWR]);
		pushredir(ROPEN, pfd[PRD], 0);
	
		runq->ret = 0;
		return;
	default:
		addwaitpid(forkid);
		break;	
	}

	start(p->code, p->code[pc].i, runq->local);
	close(pfd[PWR]);
	close(pfd[PRD]);

	p->pc = p->code[pc+1].i;
}

void
Xfanout(void)
{
	int i, j, k;
	char s[40];
	struct thread *p = runq;
	int pc = p->pc, forkid;
	int lfd = p->code[pc++].i;
	int rfd = p->code[pc++].i;
	int pfd[2];
	pipes *m = popmp();

	if(m == nil)
		Xerror("no multipipes on stack");	
	if(pipe(pfd)<0){
		Xerror("can't get pipe");
		return;
	}
	// ORF
	switch(forkid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		pushlist();
		for(i = 0; i < m->npipe; i++){
			pushword(smprint("%d", m->fd[i][0][PWR]));
		}
		startargv(p->code, pc+3, runq->local, runq->argv);
		runq->ret = 0;
		for(i = 0; i < m->npipe; i++){
			close(m->fd[i][1][PRD]);
			close(m->fd[i][1][PWR]);
			close(m->fd[i][0][PRD]); 
		} 		
		close(pfd[PWR]);
		close(1); // orf doesn't need a stdout.
		pushredir(ROPEN, pfd[PRD], 0);
		return;
	default:
		addwaitpid(forkid);
		break;
	}
	for(i = 0; i < m->npipe; i++) switch(forkid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(p->code, p->code[pc+1].i, runq->local);
		runq->ret = 0;
		close(pfd[PRD]);
		close(pfd[PWR]);
		for(j = 0; j < m->npipe; j++){
			if(j == i)
				continue;
			for(k = 0; k < 2; k++){
				close(m->fd[j][k][PWR]);
				close(m->fd[j][k][PRD]);
			}
		}
		close(m->fd[i][0][PWR]);
		close(m->fd[i][1][PRD]);
		pushredir(ROPEN, m->fd[i][0][PRD], 0);
		pushredir(ROPEN, m->fd[i][1][PWR], 1);
		return;
	default:
		addwaitpid(forkid);
		break;	
	}	
	start(p->code, p->code[pc].i, runq->local);
	for(i = 0; i < m->npipe; i++){
		for(j = 0; j < 2; j++){
			close(m->fd[i][j][PRD]);
			close(m->fd[i][j][PWR]);
		}
	} 
	close(pfd[PRD]); 
	pushredir(ROPEN, pfd[PWR], 1);
	p->pc = p->code[pc+1].i;
	p->pid = forkid;
}

enum { Wordmax = 8192, };

/*
 * Who should wait for the exit from the fork?
 */
void
Xbackq(void)
{
	int c, pid;
	int pfd[2];
	char wd[Wordmax + 1];
	char *s, *ewd = &wd[Wordmax], *stop;
	struct io *f;
	var *ifs = vlook("ifs");
	word *v, *nextv;

	stop = ifs->val? ifs->val->word: "";
	if(pipe(pfd)<0){
		Xerror("can't make pipe");
		return;
	}
	switch(pid = fork()){
	case -1:
		Xerror("try again");
		close(pfd[PRD]);
		close(pfd[PWR]);
		return;
	case 0:
		clearwaitpids();
		close(pfd[PRD]);
		start(runq->code, runq->pc+1, runq->local);
		pushredir(ROPEN, pfd[PWR], 1);
		return;
	default:
		addwaitpid(pid);
		close(pfd[PWR]);
		f = openfd(pfd[PRD]);
		s = wd;
		v = 0;
		/*
		 * this isn't quite right for utf.  stop could have utf
		 * in it, and we're processing the input as bytes, not
		 * utf encodings of runes.  further, if we run out of
		 * room in wd, we can chop in the middle of a utf encoding
		 * (not to mention stepping on one of the bytes).
		 * presotto's Strings seem like the right data structure here.
		 */
		while((c = rchr(f))!=EOF){
			if(strchr(stop, c) || s==ewd){
				if(s!=wd){
					*s='\0';
					v = newword(wd, v);
					s = wd;
				}
			}
			else *s++=c;
		}
		if(s!=wd){
			*s='\0';
			v = newword(wd, v);
		}
		closeio(f);
		Waitfor(pid, 0);
		/* v points to reversed arglist -- reverse it onto argv */
		while(v){
			nextv = v->next;
			v->next = runq->argv->words;
			runq->argv->words = v;
			v = nextv;
		}
		runq->pc = runq->code[runq->pc].i;
		return;
	}
}

void
Xpipefd(void)
{
	struct thread *p = runq;
	int pc = p->pc, pid;
	char name[40];
	int pfd[2];
	int sidefd, mainfd;
	if(pipe(pfd)<0){
		Xerror("can't get pipe");
		return;
	}
	if(p->code[pc].i==READ){
		sidefd = pfd[PWR];
		mainfd = pfd[PRD];
	}
	else{
		sidefd = pfd[PRD];
		mainfd = pfd[PWR];
	}
	switch(pid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(p->code, pc+2, runq->local);
		close(mainfd);
		pushredir(ROPEN, sidefd, p->code[pc].i==READ?1:0);
		runq->ret = 0;
		break;
	default:
		addwaitpid(pid);
		close(sidefd);
		pushredir(ROPEN, mainfd, mainfd);	/* isn't this a noop? */
		strcpy(name, Fdprefix);
		inttoascii(name+strlen(name), mainfd);
		pushword(name);
		p->pc = p->code[pc+1].i;
		break;
	}
}

void
Xsubshell(void)
{
	int pid;
	switch(pid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(runq->code, runq->pc+1, runq->local);
		runq->ret = 0;
		break;
	default:
		addwaitpid(pid);
		Waitfor(pid, 1);
		runq->pc = runq->code[runq->pc].i;
		break;
	}
}

int
execforkexec(void)
{
	int pid;
	int n;
	char buf[ERRMAX];

	switch(pid = fork()){
	case -1:
		return -1;
	case 0:
		clearwaitpids();
		pushword("exec");
		execexec();
		strcpy(buf, "can't exec: ");
		n = strlen(buf);
		errstr(buf+n, ERRMAX-n);
		Exit(buf);
	}
	addwaitpid(pid);
	return pid;
}
