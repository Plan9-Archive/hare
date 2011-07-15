/*
	gang2fs - aggregated execution management (take 2)

	Copyright (C) 2011, IBM Corporation, 
 		Eric Van Hensbergen (bergevan@us.ibm.com)

	Description:
		This provides a file system which interacts with execfs(4)
		to provide a gang execution environment.  This initial version
		will be single-node, but the intention is to support multinode
		environments

	Based in part on Pravin Shinde's devtask.c

*/

enum {	/* DEBUG LEVELS */
	DERR = 0,	/* error */
	DCUR = 1,	/* current - temporary trace */
	DPIP = 2,	/* interactions with multipipes */
	DGAN = 3,	/* gang tracking */
	DEXE = 4,	/* execfs interactions */
	DBCA = 5,	/* broadcast operations */
	DCLN = 7,	/* cleanup tracking */
	DFID = 8,	/* fid tracking */
	DREF = 9,	/* ref count tracking */
	DARG = 10,	/* arguments */
	DSTS = 11,	/* status */
};