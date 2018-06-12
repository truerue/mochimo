/* tag.c  Tag a big Mochimo address with a little name
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 17 May 2018
 *
*/

/* #define PATCHLEVEL 25 */

/*

   [<---2196 Bytes Address--->][<--12 Bytes Tag-->}

   1. A transaction may, optionally, include a random value in the
      12-byte Tag field of the change address.
   2. The source address including tag field, must be in the ledger,
      otherwist reject TX.

   A person may send to a Tag (Mochimo Account).  However, when 
   they do that, the following network transaction occurs. The 
   wallet will always send a normal TX, however, it will perform 
   a look up with the server, to find the currently bound address 
   for that tag and remit that information to the requesting 
   wallet.  The wallet therefore can take in a persistent tag 
   from the user, and use it to transparently collect the correct 
   destination address.

   12-byte tag: 00 00 00 00   00 00 00 00 00 00 00 00
                reserved      7-bytes              1-byte checksum

   Sending to a Tag:

   1. Alice tells Bob her Mochimo account is: 
      12345
      Bob keys this into his wallet as the destination address.
   2. The wallet calls a Mochimo server and sends a query: 
      OP_RESOLVE, with this TAG as a payload.
   3. The server checks to see if the Tag is bound to any address, 
      and returns the 2208-byte address it is bound to.
   4. Bob's wallet crafts a transaction with the first 2196 byte of 
      the address + the 12-byte tag as the destination and sends it.
   5. The server receives the transaction, and sees the 12-byte tag.
   6. The server looks up the Tag and confirms that it is bound to 
      the address indicated, and validates the transaction.
*/


#define ADDR_TAG_PTR(addr) (((byte *) addr) + 2196)
#define ADDR_TAG_LEN 12
#define HAS_TAG(addr) (((byte *) addr)[2196] != 0x42)

/* Tag system globals */
byte *Tagidx;  /* in-core tag index -- malloc() */
long Ntagidx;  /* number of 12-byte entries in Tagidx[] */


/* Build the address tag index.
 * Returns: 0 on success, else error code.
 * On successful return, Tagidx allocated with Ntagidx 12-byte entries,
 * else Tagidx == NULL and Ntagidx == 0.
 */
int tag_build(void)
{
   FILE *fp;
   long offset, count;
   LENTRY le;
   byte *tp;
   int ecode;

   if(Trace) plog("build_tidx(): building tag index...");

   if(Tagidx) {
      free(Tagidx);
      Tagidx = NULL;
   }

   ecode = 1;
   fp = fopen("ledger.dat", "rb");
   if(fp == NULL) goto bad2;
   ecode = 2;
   fseek(fp, 0, SEEK_END);
   offset= ftell(fp);
   if((offset % sizeof(LENTRY)) != 0) {
      error("build_tidx() bad ledger.dat modulus!");
bad:
      fclose(fp);
bad2:
      Ntagidx = 0;
      error("build_tidx() failed with code %d", ecode);
      return ecode;
   }
   ecode = 3;
   Ntagidx = offset / sizeof(LENTRY);
   if(Ntagidx < 1) goto bad;
   ecode = 4;
   Tagidx = malloc(Ntagidx * ADDR_TAG_LEN);
   if(Tagidx == NULL) goto bad;

   ecode = 5;
   fseek(fp, 0, SEEK_SET);  /* rewind ledger.dat */
   for(tp = Tagidx, count = 0; ; tp += ADDR_TAG_LEN, count++) {
      if(fread(&le, 1, sizeof(LENTRY), fp) != sizeof(LENTRY)) break;
      memcpy(tp, ADDR_TAG_PTR(le.addr), ADDR_TAG_LEN);
   }
   if(Ntagidx != count) {
      free(Tagidx);
      Tagidx = NULL;
      goto bad;
   }
   fclose(fp);
   return 0;  /* success */
}  /* end tag_build() */


void tag_free(void)
{
   if(Tagidx) {
      free(Tagidx);
      Tagidx = NULL;
   }
   Ntagidx = 0;
}


#ifdef LONG64
#if ADDR_TAG_LEN != 12
   ADDR_TAG_LEN must be 12
#endif
#endif
#if ADDR_TAG_LEN < 4
   ADDR_TAG_LEN must be >= 4
#endif

/* Find an address tag, addr, in ledger.dat.
 * On entry:
 * The tag to query is in the tag field of addr
 * and le points to an empty LENTRY buffer
 * different from addr.
 * On return:
 * If found, le.addr holds address with tag,
 * le.balance holds ledger balance, and *position
 * has file offset in ledger.dat of match.
 * If not found, *postion is set to -1.
*/
int tag_find(byte *addr, LENTRY *le, long *position)
{
   int lockfd;
   FILE *fp;
   int ecode;
   byte *tp, *tag;
   long idx, offset;
   word32 prefix4;
   unsigned long tail8;

   *position = -1;

   if(Tagidx == NULL) {
      tag_build();
      if(Tagidx == NULL) return VERROR;
   }

   tag = ADDR_TAG_PTR(addr);
   prefix4 = *((word32 *) tag);  /* first 4 bytes of tag */
#ifdef LONG64
   tail8 = *((unsigned long *) (tag + 4));
   for(tp = Tagidx, idx = 0; idx < Ntagidx; idx++, tp += ADDR_TAG_LEN) {
      if(prefix4 == *((word32 *) tp)
         && tail8 == *((unsigned long *) (tp + 4))) break;
   }
#else
   tag += 4;  /* ptr --> rest of tag */
   for(tp = Tagidx, idx = 0; idx < Ntagidx; idx++, tp += ADDR_TAG_LEN) {
      if(prefix4 == *((word32 *) tp)
         && memcmp(tag, tp + 4, ADDR_TAG_LEN - 4) == 0) break;
   }
#endif
   if(idx >= Ntagidx) return VEOK;  /* tag not found */

   /* idx is now set to found tag index. */

   /* lock TX file */
   lockfd = lock("txq1.lck", 10);
   if(lockfd == -1) {
      error("tag_find(): Cannot lock txq1.lck");
      return VERROR;
   }

   ecode = VERROR;
   fp = fopen("ledger.dat", "rb");
   if(fp == NULL) {
      error("tag_find(): Cannot open ledger.dat");
      goto err;
   }

   offset = idx * sizeof(LENTRY);
   if(fseek(fp, offset, SEEK_SET)) goto err;
   if(fread(le, 1, sizeof(LENTRY), fp) != sizeof(LENTRY)) goto err;
   *position = offset;
   ecode = VEOK;
err:
   if(fp) fclose(fp);
   unlock(lockfd);
   return ecode;
}  /* end tag_find() */


int tag_valid(byte *src_addr, byte *chg_addr)
{
   LENTRY le;
   long position;

   if(!HAS_TAG(chg_addr)) return VEOK;
   if(memcmp(ADDR_TAG_PTR(src_addr),
             ADDR_TAG_PTR(chg_addr), ADDR_TAG_LEN) == 0) goto good;
   if(HAS_TAG(src_addr)) goto bad;
   if(tag_find(chg_addr, &le, &position) != VEOK) goto bad;
   if(position != -1) goto bad;
good:
   if(Trace) plog("tag accepted");
   return VEOK;
bad:
   if(Trace) plog("tag rejected");
   return VERROR;
}


#ifndef EXCLUDE_RESOLVE

/* Look-up and return an address tag to np.
 * Called from gettx() opcode == OP_RESOLVE
 *
 * on entry:
 *     tag string at ADDR_TAG_PTR(np->tx.dst_addr)    tag to query
 * on return:
 *     np->tx.dst_addr has full found address with tag.
 *     np->tx.send_total = 1 if found, or 0 if not found.
 *
 * Returns VERROR on I/O errors, else VEOK.
*/
int tag_resolve(NODE *np)
{
   LENTRY le;
   long position;
   static byte zeros[8];
   int ecode = VEOK;

   put64(np->tx.send_total, zeros);
   /* Find tag in ledger. -- tag_find() locks/unlocks TX file. */
   if(tag_find(np->tx.dst_addr, &le, &position) == VEOK) {
      if(position != -1) {
         memcpy(np->tx.dst_addr, le.addr, TXADDRLEN);
         put64(np->tx.send_total, One);
      }
   } else ecode = VERROR;
   send_op(np, OP_RESOLVE);
   return ecode;
}  /* end tag_resolve() */

#endif /* EXCLUDE_RESOLVE */
