
/* 
8c pxcpu.c &&
8l -o pxcpu pxcpu.8 &&
cp pxcpu $home/bin/386 &&
mount -nc /srv/csrv /n/csrv && xsdo 


&&
9fs tcp!10.12.0.72!5670 && xcdo 

./pxcpu 
*/
#include <u.h>
#include <libc.h>
#include <bio.h>





void
usage(char *me)
{
	fprint(2, "%s: usage\n", me);
	exits("usage");
}
char *mnt;
char sess[64];

void
main(int argc, char **argv)
{
	char *prog;
	Biobuf *in,*out, *stdout;
	int pid;
	int n, i, j, subsess, nres, r;
	int cfd, *resfdctl, nfdctl, fd;
	char fdfile[512];
	char buf[8192];
	int qidpath;
	char *s, *line;
	char *a[4], *b[2];
	

	mnt = "/n/csrv/local";
	ARGBEGIN{
	}ARGEND
	if(argc != 0)
		usage(argv[0]);

	snprint(fdfile, 512, "%s/clone", mnt);
	cfd = open(fdfile, ORDWR);
	n = read(cfd, sess, 64);
	if(n < 0)
		sysfatal("couldn't read ctl");
	sess[n]=0;
	print("sess %s\n", sess);
	in = Bopen("/fd/0", OREAD);
	if(in == nil)
		sysfatal("couldn't open stdin");
	line = Brdstr(in, '\n', '\n');
	if(line == nil)
		sysfatal("eof");
	n = tokenize(line, a, 4);
	if(n != 2)
		sysfatal("two fields in a res");
	if(strcmp("res", a[0]) != 0)
		sysfatal("need to res in the beginning");
	fprint(cfd, "%s %s", a[0], a[1]);
	nres = atoi(a[1]);
	if(nres <= 0)
		sysfatal("need >0 res");
	resfdctl = malloc(nres*sizeof(int));
	subsess = -1; // start from 0.
	i = 0;
	r = nres;
	while(r--){
		if(i == 0)
			subsess++;
		snprint(fdfile, 512, "%s/%s/%d/%d/ctl", mnt, sess, subsess, i%4);
		print("fdfile %s\n", fdfile);
		resfdctl[i]=open(fdfile, ORDWR);
		if(resfdctl[i] < 0)
			sysfatal("couldn't open part of res %s", fdfile);
		i = (i + 1) % 4;
	}
	while((line = Brdstr(in, '\n', '\n')) != nil){
		n = getfields(line, a, 4, 1, "\t\r\n ");
		if(strcmp("splice", a[0]) == 0){
			if(n != 3)
				sysfatal("splice has 2 args");
			i = atoi(a[2]);
			if(i < 0 || nres <= i)
				sysfatal("invalid reservation %d", i);
			j = atoi(a[1]);
			if(j < 0 || nres <= j)
				sysfatal("invalid splice reservation %d", j);	
			print("splice csrv/local/%s/%s/stdio", sess, j/4, j%4);
			fprint(resfdctl[i], "splice csrv/local/%s/%d/%d/stdio", sess, j/4, j%4);
		}else if(strcmp("exec", a[0]) == 0){
			if(n < 3)
				sysfatal("exec needs >3 args");
			i = atoi(a[1]);
			if(i < 0 || nres <= i)
				sysfatal("bad splice out res");
			if(strcmp("filt", a[1]) == 0){
				i = atoi(a[3]);
				fprint(resfdctl[i], "exec %s %s %s", a[2], sess, a[3]);
			}else if(n == 4)
				fprint(resfdctl[i], "exec %s %s", a[2], a[3]);
			else
				fprint(resfdctl[i], "exec %s", a[2]);
		}else if(strcmp("end", a[0]) == 0){
			if(n != 2)
				sysfatal("end needs 1 arg");
			snprint(fdfile, 512, "%s/%s/%s/stdio", mnt, sess, a[1]);
			print("reading from %s\n", fdfile);
			fd = open(fdfile, OREAD);
			if(fd < 0)
				sysfatal("couldn't open: %s %r", fdfile);
		}else
			sysfatal("invalid command string");
	}
	stdout = Bopen("/fd/1", OWRITE);
	if(stdout == nil)
		sysfatal("couldn't open stdout");
	print("reading\n");
	while((n = read(fd, buf, 8192)) > 0){
		print("read n %d %.*s\n", n, n, buf);
		write(1, buf, n);
		Bflush(stdout);
	}
	print("read n %d %.*s\n", n, n, buf);
	close(fd);
	Bterm(in);
	exits(0);
}

