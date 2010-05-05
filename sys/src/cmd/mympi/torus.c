/*#include <stdio.h>
#include <stdlib.h>
#include <string.h>
*/
#include <u.h>
#include <libc.h>
#include "mpi.h"
#define F(v, o, w)	(((v) & ((1<<(w))-1))<<(o))
typedef unsigned char u8int;
typedef unsigned long long u64int;
enum {
	X		= 0,			/* dimension */
	Y		= 1,
	Z		= 2,
	N		= 3,

	Chunk		= 32,			/* granularity of FIFO */
	Pchunk		= 8,			/* Chunks in a packet */

	Quad		= 16,
	Tpkthdrlen	= 16,
};

/*
 * Packet header. The hardware requires an 8-byte header
 * of which the last two are reserved (they contain a sequence
 * number and a header checksum inserted by the hardware).
 * The hardware also requires the packet to be aligned on a
 * 128-bit boundary for loading into the HUMMER.
 */
typedef struct Tpkt Tpkt;
struct Tpkt {
	u8int	sk;				/* Skip Checksum Control */
	u8int	hint;				/* Hint|Dp|Pid0 */
	u8int	size;				/* Size|Pid1|Dm|Dy|VC */
	u8int	dst[N];				/* Destination Coordinates */
	u8int	_6_[2];				/* reserved */
	u8int	_8_[8];				/* protocol header */
	u8int	payload[];
};

/*
 * SKIP is a field in .sk giving the number of 2-bytes
 * to skip from the top of the packet before including
 * the packet bytes into the running checksum.
 * SIZE is a field in .size giving the size of the
 * packet in 32-byte 'chunks'.
 */
#define SKIP(n)		F(n, 1, 7)
#define SIZE(n)		F(n, 5, 3)

enum {
	Sk		= 0x01,			/* Skip Checksum */

	Pid0		= 0x01,			/* Destination Group FIFO MSb */
	Dp		= 0x02,			/* Multicast Deposit */
	Hzm		= 0x04,			/* Z- Hint */
	Hzp		= 0x08,			/* Z+ Hint */
	Hym		= 0x10,			/* Y- Hint */
	Hyp		= 0x20,			/* Y+ Hint */
	Hxm		= 0x40,			/* X- Hint */
	Hxp		= 0x80,			/* X+ Hint */

	Vcd0		= 0x00,			/* Dynamic 0 VC */
	Vcd1		= 0x01,			/* Dynamic 1 VC */
	Vcbn		= 0x02,			/* Deterministic Bubble VC */
	Vcbp		= 0x03,			/* Deterministic Priority VC */
	Dy		= 0x04,			/* Dynamic Routing */
	Dm		= 0x08,			/* DMA Mode */
	Pid1		= 0x10,			/* Destination Group FIFO LSb */
};


int debug = 0;
int x, y, z;
int xsize, ysize, zsize;
int torusfd = -1;

void panic(char *s);

static int
torusparse(u8int d[3], char* item, char* buf)
{
	int n;
	char *p;

	if((p = strstr(buf, item)) == nil || (p != buf && *(p-1) != '\n'))
		return -1;
	n = strlen(item);
	if(strlen(p) < n+sizeof(": x 0 y 0 z 0"))
		return -1;
	p += n+sizeof(": x ")-1;
	if(strncmp(p-4, ": x ", 4) != 0)
		return -1;
	if((n = strtol(p, &p, 0)) > 255 || *p != ' ' || *(p+1) != 'y')
		return -1;
	d[0] = n;
	if((n = strtol(p+2, &p, 0)) > 255 || *p != ' ' || *(p+1) != 'z')
		return -1;
	d[1] = n;
	if((n = strtol(p+2, &p, 0)) > 255 || (*p != '\n' && *p != '\0'))
		return -1;
	d[2] = n;

	return 0;
}

struct xyz {
	int x, y, z, kids;
};

struct xyz *xyz;
int *ranks;

void torusinit(int *pmyproc, const int numprocs)
{
	char buf[512];
	int fd;
	u8int d[3];
	int n, rank;
	int i, j = 0, k = 0;
	int maxprocs;

	if((fd = open("/dev/torusstatus", 0)) < 0)
		panic("open /dev/torusstatus: %r\n");
	if((n = read(fd, buf, sizeof(buf))) < 0)
		panic("read /dev/torusstatus: %r\n");
	close(fd);
	buf[n] = 0;

	torusfd = open("/dev/torus", 2);
	if (torusfd < 0)
		panic("opening torus");

	if(torusparse(d, "size", buf) < 0)
		panic("parse /dev/torusstatus");
	xsize = d[X];
	ysize = d[Y];
	zsize = d[Z];

	if(torusparse(d, "addr", buf) < 0)
		panic("parse /dev/torusstatus");
	x = d[X];
	y = d[Y];
	z = d[Z];
print( "%d/%d %d/%d %d/%d\n", x, xsize, y, ysize, z, zsize);
print( "numprocs %d \n", numprocs);
	/* make some tables. Mapping is done as it is to maximally distributed broadcast traffic */
	xyz = calloc(numprocs, sizeof(*xyz));
	if (!xyz)
		panic("mpi init xyz");
	ranks = calloc(numprocs, sizeof(*ranks));
	if (!ranks)
		panic("mpi init ranks");

	/* root node */
	/* base case -- for all nods, set basic values */
	xyz[0].x = 0;
	ranks[0] = 0;
	/* base case -- if I am the root node, fill this in */
	if (x == y == z == 0){
		*pmyproc = 0;
		print( "set myproc to %d @ (%d, %d, %d)\n",
				0, x, y, z);
	}
	/* first level -- X axis (1-xsize-1, 0, 0) -- lowest order ranks go on this axis. */
	/* in all cases, if we match 'me', fill that value in */
	for(rank = 1, i = 1; (i < xsize) && (rank < numprocs); i++, rank++) {
		xyz[0].kids++;
		xyz[rank].x = i;
		if ((x == i) && (y == j) && (z == k)) {
			*pmyproc = rank;
			print( "set myproc to %d @ (%d, %d, %d)\n",
				rank, x, y, z);
		}
		ranks[i] = rank;
	}

	/* second level */
	/* second level -- X axis (1-xsize-1, 1-ysize-1, 0) -- lowest order ranks go on this axis. */
	for(i = 1; (i < xsize) && (rank < numprocs); i++) {
		for(j = 1; (j < ysize) && (rank < numprocs); j++, rank++) {
			xyz[i].kids++;
			xyz[rank].x = i;
			xyz[rank].y = j;
			if ((x == i) && (y == j) && (z == k)){
				*pmyproc = rank;
				print( "set myproc to %d @ (%d, %d, %d)\n",
					rank, x, y, z);
			}
			ranks[i + j*ysize] = rank;
		}
	}

	/* third level */
	/* second level -- X axis (1-xsize-1, 1-ysize-1, 1-zsize-1) -- lowest order ranks go on this axis. */
	for(i = 1; (i < xsize) && (rank < numprocs); i++) {
		for(j = 1; (j < ysize) && (rank < numprocs); j++) {
			for(k = 1; (k < zsize) && (rank < numprocs); k++, rank++) {
				xyz[i*xsize +ysize].kids++;
				xyz[rank].x = i;
				xyz[rank].y = j;
				xyz[rank].z = k;
				if ((x == i) && (y == j) && (z == k)){
					*pmyproc = rank;
					print( "set myproc to %d @ (%d, %d, %d)\n",
						rank, x, y, z);
				}
				ranks[i + j*ysize + k*xsize*ysize] = rank;
			}
		}
	}
	
}
static void
dumptpkt(Tpkt* tpkt, int hflag, int dflag)
{
	u8int *t;
	int i;

	if(hflag){
		print("Hw:");
		t = (u8int*)tpkt;
		for(i = 0; i < 8; i++)
			print(" %2.2ux", t[i]);
		print("\n");

		print("Sw:");
		t = (u8int*)tpkt->_8_;
		for(i = 0; i < 8; i++)
			print(" %#2.2ux", t[i]);
	}

	if(!dflag)
		return;
}

void
ranktoxyz(int rank, int *x, int *y, int *z)
{
	extern int maxrank;
	
	if (rank > maxrank)
		panic("ranktoxyz: rank too large");
	*x = xyz[rank].x;
	*y = xyz[rank].y;
	*z = xyz[rank].z;
}

int
xyztorank(int x, int y, int z)
{
	int rank;
	rank = ranks[x + y * ysize + z*ysize*xsize];
	return rank;
}


/* failure is not an option */
/* note this is pretty awful ... copy to here, then copy in kernel!  */
void
torussend(void *buf, int length, int rank, void *tag, int taglen)
{
	int n;
	/* OMG! We're gonna copy AGAIN. Mantra: right then fast. */
	Tpkt *tpkt;
	int x, y, z;
	u8int *packet;

	packet = malloc(length + taglen + sizeof(*tpkt));
	tpkt = (Tpkt *)packet;
	memcpy(tpkt->payload, tag, taglen);
	memcpy(tpkt->payload + taglen, buf, length);

	ranktoxyz(rank, &x, &y, &z);
	tpkt->dst[X] = x;
	tpkt->dst[Y] = y;
	tpkt->dst[Z] = z;
	
	n = pwrite(torusfd, tpkt, length + taglen + sizeof(*tpkt), 0);
	if (debug & 1)
		dumptpkt(tpkt, 1, 1);
	if(n != length)
		panic("write /dev/torus");
	free(tpkt);
}

int
torusrecv(MPI_Status **status, void *buf, long length, void *tag, long taglen)
{
	int n;

	n = pread(torusfd, tag, taglen, 0);
	if (n < 0)
		panic("torusrecv tag");
	n = pread(torusfd, buf, length, 0);
	if (n < 0)
		panic("torusrecv buf");
	if (n ==0)
		return n;

	if (debug & 1)
		dumptpkt((Tpkt *)buf, 1, 1);

	*status = (MPI_Status *) ((u8int *)buf + sizeof(Tpkt));
	return n;
}
