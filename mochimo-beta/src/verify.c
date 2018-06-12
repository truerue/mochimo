/* verify.c  Verify a WOTS signature
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 11 May 2018
 *
*/



#ifdef SIGN_TX

void sign_tx(TX *tx, byte *secret)
{
   static byte hb[32+8+32] = "Veronica";
   byte message[32], rnd2[32];

   sha256(tx->src_addr, SIG_HASH_COUNT, &hb[(32+8)]);
   put64(&hb[32], tx->send_total);
   sha256(hb, (32+8+32), message);  /* collision-resilient message digest */

   /* sign TX with secret key for src_addr*/
   memcpy(rnd2, &tx->src_addr[TXSIGLEN+32], 32);  /* temp for wots_sign() */
   wots_sign(tx->tx_sig,   /* output 2144 */
             message,      /* message hash 32 */
             secret,       /* random secret key 32 */
             &tx->src_addr[TXSIGLEN],    /* rnd1 32 */
             (word32 *) rnd2             /* rnd2 32 (modified) */
   );
}  /* end sign_tx() */

#else

/* Verify transaction signature using 
 * WOTS on a collision-resilient message digest.
 * Return VEOK on success, else VERROR.
 */
int verify_tx(TX *tx)
{
   static byte hb[32+8+32] = "Veronica";
   static byte pk2[TXSIGLEN];
   byte message[32], rnd2[32];

   sha256(tx->src_addr, SIG_HASH_COUNT, &hb[(32+8)]);
   put64(&hb[32], tx->send_total);
   sha256(hb, (32+8+32), message);  /* collision-resilient message digest */

   /* check WTOS signature */
   memcpy(rnd2, &tx->src_addr[TXSIGLEN+32], 32);  /* copy WOTS addr[] */
   wots_pk_from_sig(pk2, tx->tx_sig, message, &tx->src_addr[TXSIGLEN],
                    (word32 *) rnd2);
   if(memcmp(pk2, tx->src_addr, TXSIGLEN) != 0) return VERROR;  /* fail */
   return VEOK;  /* success -- signature verified */
}  /* end verify_tx() */

#endif
