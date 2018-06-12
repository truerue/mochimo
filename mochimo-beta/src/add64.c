/* add64.c  64-bit integer assist for little-endian 32-bit machines
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 3 January 2018
 *
 * NOTE: Code is little-endian.
 */


/* Add *ap to *bp giving *cp.  Result at *cp. */
int add64(void *ap, void *bp, void *cp)
{
   word32 t, *a, *b, *c;
   int carry;

   a = ap; b = bp; c = cp;
   t = a[0];
   c[0] = a[0] + b[0];
   if(c[0] < t) carry = 1; else carry = 0;
   t = a[1];
   c[1] = a[1] + b[1] + carry;
   if(c[1] < t) carry = 1; else carry = 0;
   return carry;
}


/* Compute *ap minus *bp giving *cp.  Result at *cp. */
int sub64(void *ap, void *bp, void *cp)
{
   word32 t, *a, *b, *c;
   int carry;

   a = ap; b = bp; c = cp;
   t = a[0];
   c[0] = a[0] - b[0];
   if(c[0] > t) carry = 1; else carry = 0;
   t = a[1];
   c[1] = a[1] - b[1] - carry;
   if(c[1] > t) carry = 1; else carry = 0;
   return carry;
}

void negate64(void *ax)
{
   word32 *a = (word32 *) ax;

   a[0] = ~a[0];
   a[1] = ~a[1];
   a[0]++;
   if(a[0] == 0) a[1]++;
}


/* Unsigned compare a to b.  a - b
 * Returns 0 if a==b, negative if a < b, or positive if a > b
 */
int cmp64(void *a, void *b)
{
   word32 *pa, *pb;

   pa = (word32 *) a;
   pb = (word32 *) b;
   if(pa[1] > pb[1]) return 1;
   if(pa[1] < pb[1]) return -1;
   if(pa[0] > pb[0]) return 1;
   if(pa[0] < pb[0]) return -1;
   return 0;
}


/* shift a 64-bit value one to the right. */
void shiftr64(void *value64)
{
   word32 *val;

   val = (word32 *) value64;
   val[0] >>= 1;
   if(val[1] & 1) val[0] |= 0x80000000;
   val[1] >>= 1;
}


void put64(void *buff, void *val);  /* forward reference */

/* Multiply *ap by *bp giving *cp.  Result at *cp.
 * Returns 1 if overflow, else 0.
 */
int mult64(void *ap, void *bp, void *cp)
{
   word32 a[2], b[2], c[2];
   int overflow = 0;

   put64(a, ap);
   put64(b, bp);
   c[0] = c[1] = 0;
   while(b[0] | b[1]) {
      if(b[0] & 1)
         overflow |= add64(c, a, c);
      add64(a, a, a);  /* shift a left */
      shiftr64(b);
   }
   put64(cp, c);
   return overflow;
}  /* end mult64() */


/* Add a[len] to b[len] giving c[len].  Result in c.
 * len = 2 for 64-bit adds.
 * len = 3 for 96-bit adds, etc.
 */
int multi_add32(void *ap, void *bp, void *cp, int len)
{
   word32 t, *a, *b, *c;
   int carry = 0;

   if(len < 1) return 0;

   a = ap; b = bp; c = cp;
   for( ; len; a++, b++, c++, len--) {
      t = a[0];
      c[0] = t + b[0] + carry;
      if(c[0] < t) carry = 1; else carry = 0;
   }
   return carry;
}

/* Subtract a[len] minus b[len] giving c[len].  Result in c.
 * len = 2 for 64-bit.
 * len = 3 for 96-bit, etc.
 */
int multi_sub32(void *ap, void *bp, void *cp, int len)
{
   word32 t, *a, *b, *c;
   int carry = 0;

   if(len < 1) return 0;

   a = ap; b = bp; c = cp;
   for( ; len; a++, b++, c++, len--) {
      t = a[0];
      c[0] = t - b[0] - carry;
      if(c[0] > t) carry = 1; else carry = 0;
   }
   return carry;
}
