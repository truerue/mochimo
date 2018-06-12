/* bupdata.c  Update Globals
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * The Mochimo Project System Software
 *
 * Date: 6 February 2018
 *
*/


/* seconds is 32-bit signed */
word32 set_difficulty(word32 difficulty, int seconds)
{
   if(seconds < 0) return difficulty;  /* when time_t overflows in 2106 */

   if(seconds > HIGHSOLVE) {  /* 506 */
      if(difficulty > 0) difficulty--;     /* 8.43333 minutes */
   } else if(seconds < LOWSOLVE) { /* 253 */
      if(difficulty < 256) difficulty++;   /* 4.21666 minutes */
   }
   return difficulty;
}


/* Called from server.c to update globals */
int bupdata(void)
{
   int ecode = VEOK;
   word32 time1;
   BTRAILER bt;

   if(add64(Cblocknum, One, Cblocknum)) /* increment block number */
      ecode = error("new blocknum overflow");

   /* Update block hashes */
   memcpy(Prevhash, Cblockhash, HASHLEN);
   if(readtrailer(&bt, "ublock.dat") != VEOK)
      ecode = error("bupdata(): cannot read new ublock.dat hash");
   memcpy(Cblockhash, bt.bhash, HASHLEN);
   Difficulty = get32(bt.difficulty);
   Time0 = get32(bt.time0);
   time1 = get32(bt.stime);
   add_weight(Weight, Difficulty);
   /* Update block difficulty */
   Difficulty = set_difficulty(Difficulty, time1 - Time0);
   if(Trace) {
      plog("new: Difficulty = %d  seconds = %d",
           Difficulty, time1 - Time0);
      plog("Cblockhash: %s for block: 0x%s", hash2str(Cblockhash),
           bnum2hex(Cblocknum));
   }
   Time0 = time1;
   return ecode;
}  /* end bupdata() */


/* Build a neo-genesis block -- called from server.c */
int do_neogen(void)
{
   char cmd[1024];
   int len;
   word32 newnum[2];
   char *cp;
   int ecode;
   BTRAILER bt;

   unlink("neofail.lck");
   cp = bnum2hex(Cblocknum);
   sprintf(cmd, "../neogen %s/b%s.bc", Bcdir, cp);
   len = strlen(cmd);
   add64(Cblocknum, One, newnum);
   cp = bnum2hex((byte *) newnum);
   sprintf(&cmd[len], " %s/b%s.bc", Bcdir, cp);
   if(Trace) plog("Creating neo-genesis block:\n '%s'", cmd);
   ecode = system(cmd);
   if(Trace) plog("do_neogen(): system():  ecode = %d", ecode);
   if(exists("neofail.lck"))
      return error("do_neogen failed");
   sprintf(cmd, "%s/b%s.bc", Bcdir, cp);

   add64(Cblocknum, One, Cblocknum);
   /* Update block hashes */
   memcpy(Prevhash, Cblockhash, HASHLEN);
   if(readtrailer(&bt, cmd) != VEOK)
      return error("do_neogen(): cannot read NG block hash");
   memcpy(Cblockhash, bt.bhash, HASHLEN);
   Eon++;
   return VEOK;
}
