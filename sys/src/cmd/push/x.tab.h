#define	FOR	57346
#define	IN	57347
#define	WHILE	57348
#define	IF	57349
#define	NOT	57350
#define	TWIDDLE	57351
#define	BANG	57352
#define	SUBSHELL	57353
#define	SWITCH	57354
#define	FN	57355
#define	WORD	57356
#define	REDIR	57357
#define	DUP	57358
#define	PIPE	57359
#define	FANIN	57360
#define	FANOUT	57361
#define	SUB	57362
#define	SIMPLE	57363
#define	FILTER	57364
#define	ARGLIST	57365
#define	WORDS	57366
#define	BRACE	57367
#define	PAREN	57368
#define	PCMD	57369
#define	PIPEFD	57370
#define	ANDAND	57371
#define	OROR	57372
#define	COUNT	57373

typedef union {
	struct tree *tree;
}	YYSTYPE;
extern	YYSTYPE	yylval;
