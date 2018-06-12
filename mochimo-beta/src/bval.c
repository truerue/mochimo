/* bval.c  Block Validator
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * The Mochimo Project System Software
 *
 * Date: 8 January 2018
 *
 * NOTE: Invoked by server.c update() by wait on system()
 *
 * Returns exit code 0 on successful validation,
 *                   1 I/O errors, or
 *                   >= 2 Peerip needs pinklist().
 *
 * Inputs:  argv[1],    rblock.dat, the block to validate
 *          ledger.dat  the ledger of address balances
 *
 * Outputs: ltran.dat  transaction file to post against ledger.dat
 *          exit status 0=valid or non-zero=not valid.
 *          renames argv[1] to "vblock.dat" on good validation.
*/


#include "config.h"
#include "mochimo.h"
#define closesocket(_sd) close(_sd)
char *trigg_check(byte *in, byte d, byte *bnum);

#define EXCLUDE_NODES   /* exclude Nodes[], ip, and socket data */
#include "data.c"

#include "error.c"
#include "rand.c"
#include "crc16.c"
#include "add64.c"
#include "util.c"
#include "daemon.c"
#include "ledger.c"

#define EXCLUDE_RESOLVE
#include "tag.c"

word32 Tnum = -1;    /* transaction sequence number */
char *Bvaldelfname;  /* set == argv[1] to delete input file on failure */


void cleanup(int ecode)
{
   unlink("ltran.tmp");
   if(Bvaldelfname) unlink(Bvaldelfname);
   if(ecode > 1)
      write_data("pink", 4, "vbad.lck");
   exit(ecode);
}

void drop(char *message)
{
   if(Trace) plog("bval: drop(): %s TX index = %d", message, Tnum);
   cleanup(3);
}


void baddrop(char *message)
{
   if(Trace) plog("bval: baddrop(): %s from: %s  TX index = %d",
                  message, ntoa((byte *) &Peerip), Tnum);
   /* add Peerip to epoch pink list */
   cleanup(3);  /* put on epink.lst */
}


void bail(char *message)
{
   error("bval: %s", message);
   cleanup(1);
}


/* Invocation: bval file_to_validate */
int main(int argc, char **argv)
{
   BHEADER bh;             /* fixed length block header */
   static BTRAILER bt;     /* block trailer */
   static TXQENTRY tx;     /* Holds one transaction in the array */
   FILE *fp;               /* to read block file */
   FILE *ltfp;             /* ledger transaction output file ltran.tmp */
   word32 hdrlen, tcount;  /* header length and transaction count */
   int cond;
   static LENTRY src_le, chg_le;    /* source and change ledger entries */
   static LENTRY dst_le;            /* destination ledger entry */
   word32 total[2];                 /* for 64-bit maths */
   static byte mroot[HASHLEN];      /* computed Merkel root */
   static byte bhash[HASHLEN];      /* computed block hash */
   static byte tx_id[HASHLEN];      /* hash of transaction and signature */
   static byte prev_tx_id[HASHLEN]; /* to check sort */
   static SHA256_CTX bctx;  /* to hash entire block */
   static SHA256_CTX mctx;  /* to hash transaction array */
   word32 bnum[2], stemp;
   static word32 mfees[2], mreward[2];
   unsigned long blocklen;
   int count;
   static byte do_rename = 1;
   static byte pk2[WOTSSIGBYTES], message[32], rnd2[32];  /* for WOTS */
   static char *haiku;


   fix_signals();
   close_extra();

   if(argc < 2) {
      printf("\nusage: bval {rblock.dat | file_to_validate} [-n]\n"
             "  -n no rename, just create ltran.dat\n"
             "This program is spawned from server.c\n\n");
      exit(1);
   }

   /* created on exit() to pinklist() Peerip in update() */
   unlink("vbad.lck");

   if(argc > 2 && argv[2][0] == '-') {
      if(argv[2][1] == 'n') do_rename = 0;
   }

   if(strcmp(argv[1], "rblock.dat") == 0) Bvaldelfname = argv[1];
   unlink("vblock.dat");
   unlink("ltran.dat");

   /* get global block number, peer ip, etc. */
   if(read_global() != VEOK)
      bail("Cannot read_global()");

   if(Trace) Logfp = fopen(LOGFNAME, "a");

   if(tag_build())
      bail("Cannot build tag index");

   /* open ledger read-only */
   if(le_open("ledger.dat", "rb") != VEOK)
      bail("Cannot open ledger.dat");

   /* create ledger transaction temp file */
   ltfp = fopen("ltran.tmp", "wb");
   if(ltfp == NULL) bail("Cannot create ltran.tmp");

   /* open the block to validate */
   fp = fopen(argv[1], "rb");
   if(!fp) {
badread:
      bail("Cannot read input rblock.dat");
   }
   if(fread(&hdrlen, 1, 4, fp) != 4) goto badread;  /* read header length */
   /* regular fixed size block header */
   if(hdrlen != sizeof(BHEADER))
      drop("bad hdrlen");

   /* compute block file length */
   if(fseek(fp, 0, SEEK_END)) goto badread;
   blocklen = ftell(fp);

   /* Read block trailer:
    * Check phash, bnum,
    * difficulty, Merkel Root, nonce, solve time, and block hash.
    */
   if(fseek(fp, -(sizeof(BTRAILER)), SEEK_END)) goto badread;
   if(fread(&bt, 1, sizeof(BTRAILER), fp) != sizeof(BTRAILER))
      drop("bad trailer read");
   if(memcmp(Mfee, bt.mfee, 8) != 0)
      drop("bad mining fee");
   if(get32(bt.difficulty) != Difficulty)
      drop("difficulty mismatch");
   stemp = get32(bt.stime);
   /* check for early block time */
   if(stemp <= Time0)   /* unsigned time here */
      drop("block time too early");
   add64(Cblocknum, One, bnum);
   if(memcmp(bnum, bt.bnum, 8) != 0)
      drop("bad block number");
   if(memcmp(Cblockhash, bt.phash, HASHLEN) != 0)
      drop("previous hash mismatch");

   /* check enforced delay */
   if((haiku = trigg_check(bt.mroot, bt.difficulty[0], bt.bnum)) == NULL)
      drop("trigg_check() failed!");
   printf("\n%s\n\n", haiku);

   /* Read block header */
   if(fseek(fp, 0, SEEK_SET)) goto badread;
   if(fread(&bh, 1, hdrlen, fp) != hdrlen)
      drop("short header read");
   get_mreward(mreward, bnum);
   if(memcmp(bh.mreward, mreward, 8) != 0)
      drop("bad mining reward");

   /* fp left at offset of Merkel Block Array--ready to fread() */

   sha256_init(&bctx);   /* begin entire block hash */
   sha256_update(&bctx, (byte *) &bh, hdrlen);  /* ... with the header */

   /*
    * Copy transaction count from block trailer and check.
    */
   tcount = get32(bt.tcount);
   if(tcount == 0 || tcount > MAXBLTX)
      baddrop("bad bt.tcount");
   if((hdrlen + sizeof(BTRAILER) + (tcount * sizeof(TXQENTRY))) != blocklen)
      drop("bad block length");

   /* Now ready to read transactions */
   sha256_init(&mctx);   /* begin Merkel Array hash */

   /* Validate each transaction */
   for(Tnum = 0; Tnum < tcount; Tnum++) {
      if(Tnum >= MAXBLTX)
         drop("too many TX's");
      if(fread(&tx, 1, sizeof(TXQENTRY), fp) != sizeof(TXQENTRY))
         drop("bad TX read");
      if(   memcmp(tx.src_addr, tx.dst_addr, TXADDRLEN) == 0
         || memcmp(tx.src_addr, tx.chg_addr, TXADDRLEN) == 0)
               drop("src_addr matched dst or chg");

      if(memcmp(Mfee, tx.tx_fee, 8) != 0)
         drop("tx_fee is bad");   /* fixed fee */

      /* running block hash */
      sha256_update(&bctx, (byte *) &tx, sizeof(TXQENTRY));
      /* running Merkel hash */
      sha256_update(&mctx, (byte *) &tx, sizeof(TXQENTRY));
      /* tx_id is hash of tx.src_add */
      sha256(tx.src_addr, TXADDRLEN, tx_id);
      if(memcmp(tx_id, tx.tx_id, HASHLEN) != 0)
         drop("bad TX_ID");

      /* Check that tx_id is sorted. */
      if(Tnum != 0) {
         cond = memcmp(tx_id, prev_tx_id, HASHLEN);
         if(cond < 0)  drop("TX_ID unsorted");
         if(cond == 0) drop("duplicate TX_ID");
      }
      /* remember this tx_id for next time */
      memcpy(prev_tx_id, tx_id, HASHLEN);

      /* check WTOS signature */
      sha256(tx.src_addr, SIG_HASH_COUNT, message);
      memcpy(rnd2, &tx.src_addr[TXSIGLEN+32], 32);  /* copy WOTS addr[] */
      wots_pk_from_sig(pk2, tx.tx_sig, message, &tx.src_addr[TXSIGLEN],
                       (word32 *) rnd2);
      if(memcmp(pk2, tx.src_addr, TXSIGLEN) != 0)
         baddrop("WOTS signature failed!");

      /* look up source address in ledger */
      if(le_find(tx.src_addr, &src_le, NULL) == FALSE)
         drop("src_addr not in ledger");

      total[0] = total[1] = 0;
      /* use add64() to check for carry out */
      cond =  add64(tx.send_total, tx.change_total, total);
      cond += add64(tx.tx_fee, total, total);
      if(cond) drop("total overflow");

      if(memcmp(src_le.balance, total, 8) < 0)  /* !=  @ */
         drop("bad transaction total");

      if(tag_valid(tx.src_addr, tx.chg_addr) != VEOK)
         drop("tag not valid");

      /* Write ledger transaction to ltran.tmp '-' first */
      fwrite(tx.src_addr,    1, TXADDRLEN, ltfp);
      fwrite("-",            1,         1, ltfp);  /* zero src addr */
      fwrite(&total,         1,         8, ltfp);
      /* add to or create dst address */
      if(!iszero(tx.send_total, 8)) {
         fwrite(tx.dst_addr,   1, TXADDRLEN, ltfp);
         fwrite("+",           1,         1, ltfp);
         fwrite(tx.send_total, 1,         8, ltfp);
      }
      /* add to or create change address */
      if(!iszero(tx.change_total, 8)) {
         fwrite(tx.chg_addr,     1, TXADDRLEN, ltfp);
         fwrite("+",             1,         1, ltfp);
         fwrite(tx.change_total, 1,         8, ltfp);
      }

      if(add64(mfees, Mfee, mfees)) {
fee_overflow:
         bail("mfees overflow");
      }
   }  /* end for Tnum */
   sha256_final(&mctx, mroot);  /* compute Merkel Root */
   if(memcmp(bt.mroot, mroot, HASHLEN) != 0)
      drop("bad Merkle root");

   sha256_update(&bctx, (byte *) &bt, sizeof(BTRAILER) - HASHLEN);
   sha256_final(&bctx, bhash);
   if(memcmp(bt.bhash, bhash, HASHLEN) != 0)
      drop("bad block hash");

   /* Create a transaction amount = mreward + (mfees = Mfee * ntx);
    * address = bh.maddr
    */
   if(add64(mfees, mreward, mfees)) goto fee_overflow;
   /* Make ledger tran to add to or create mining address.
    * '...Money from nothing...'
    */
   count =  fwrite(bh.maddr, 1, TXADDRLEN, ltfp);
   count += fwrite("+",   1,         1, ltfp);
   count += fwrite(mfees, 1,         8, ltfp);
   if(count != (TXADDRLEN+1+8) || ferror(ltfp))
      drop("ltfp I/O error");

   le_close();
   fclose(ltfp);
   fclose(fp);
   rename("ltran.tmp", "ltran.dat");
   unlink("vblock.dat");
   if(do_rename)
      rename(argv[1], "vblock.dat");
   if(Trace) plog("bval: block validated to vblock.dat");
   if(argc > 2) printf("Validated\n");
   return 0;  /* success */
}  /* end main() */
