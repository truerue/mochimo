/* proto.h  Mochimo function prototypes
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 28 February 2018
 *
*/

void close_extra(void);
int write_data(void *buff, int len, char *fname);

/* Source file: update.c */
int send_found(void);
void wait_tx(void);
int update(char *fname, int mode);

/* Source file: gettx.c */
int freeslot(NODE *np);
int sendtx(NODE *np);
int send_op(NODE *np, int opcode);
int check_contention(NODE *np);
int gettx(NODE *np, SOCKET sd);
NODE *getslot(NODE *np);

/* Source file: execute.c */
int process_tx(NODE *np);
int sendnack(NODE *np);
int send_file(NODE *np, char *fname);
int send_ipl(NODE *np);
int copy_rec_ipl(TX *tx);
int execute(NODE *np);

/* Source file: contend.c */
int rx2(NODE *np, int checkids, int seconds);
int callserver(NODE *np, word32 ip);
int get_tx2(NODE *np, word32 ip, word16 opcode);
int get_block2(word32 ip, byte *bnum, char *fname, word16 opcode);
int contend(word32 ip);

/* Source file: init.c */
int read_coreipl(char *fname);
word32 init_coreipl(NODE *np, char *fname);
void add_weight(byte *weight, int difficulty);
int append_tfile(char *fname, char *tfile);
byte *tfval(char *fname, byte *highblock, int weight_only, int *result);
int get_eon(NODE *np, word32 peerip);
int init(void);
void trigg_solve(byte *link, int diff, byte *bnum);
char *trigg_generate(byte *in, int diff);
char *trigg_check(byte *in, byte d, byte *bnum);

void stop_mirror(void);
int send_balance(NODE *np);
