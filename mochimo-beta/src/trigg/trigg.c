/*** CONFIDENTIAL information of the Mochimo Dev Team ***/

/* Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 */

   
#include "../config.h"

#include "../sha256.h"

#include <stdlib.h>

#include <string.h>

#define TRIGG_FAIL NULL

#define T          1

#define NIL        0
typedef word32 FE;  typedef struct { byte tok[12];  FE fe;
  } DICT;  
#define F_ING       1

#define F_INF       2

#define F_MOTION    4

#define F_VB        (F_INT | F_INT | F_MOTION)

#define F_NS        8

#define F_NPL       16

#define F_N         (F_NS | F_NPL)

#define F_MASS      32

#define F_AMB       64

#define F_TIMED     128

#define F_TIMEY     256

#define F_TIME      (F_TIMED | F_TIMEY)

#define F_AT        512

#define F_ON        1024

#define F_IN        2048

#define F_LOC       (F_AT | F_ON | F_IN)

#define F_NOUN      (F_NS | F_NPL | F_MASS | F_TIME | F_LOC)

#define F_PREP      4096

#define F_ADJ       8192

#define F_OP        16384

#define F_DETS      32768

#define F_DETPL     0x10000

#define F_XLIT      0x20000

#define S_NL    (F_XLIT + 1)

#define S_CO    (F_XLIT + 2)

#define S_MD    (F_XLIT + 3)

#define S_LIKE  (F_XLIT + 4)

#define S_A     (F_XLIT + 5)

#define S_THE   (F_XLIT + 6)

#define S_OF    (F_XLIT + 7)

#define S_NO    (F_XLIT + 8)

#define S_S       (F_XLIT + 9)

#define S_AFTER   (F_XLIT + 10)

#define S_BEFORE  (F_XLIT + 11)

#define S_AT     (F_XLIT + 12)

#define S_IN     (F_XLIT + 13)

#define S_ON     (F_XLIT + 14)

#define S_UNDER  (F_XLIT + 15)

#define S_ABOVE  (F_XLIT + 16)

#define S_BELOW  (F_XLIT + 17)

#define MAXDICT 256

#define MAXH    16

#include "tdict.c"
static int Tdiff; static byte Tchain[32+256+16+8];   static
FE Frame[][MAXH] = {  { F_PREP, F_ADJ, F_MASS, S_NL, F_NPL,
S_NL, F_INF | F_ING }, { F_PREP, F_MASS, S_NL, F_ADJ, F_NPL,
S_NL, F_INF | F_ING }, { F_PREP, F_TIMED, S_NL, F_ADJ, F_NPL,
S_NL, F_INF | F_ING }, { F_PREP, F_TIMED, S_NL, S_A, F_NS,
S_NL, F_ING },  { F_TIME, F_AMB, S_NL, F_PREP, S_A, F_ADJ,
F_NS, S_MD, S_NL, F_ADJ | F_ING }, { F_TIME, F_AMB, S_NL, 
F_ADJ, F_MASS, S_NL, F_ING },  { F_TIME, F_MASS, S_NL, 
F_INF, S_S, S_CO, S_NL, F_AMB },  { F_ING, F_PREP, S_A,
F_ADJ, F_NS, S_NL, F_MASS, F_ING, S_MD, S_NL, S_A, F_ADJ,
F_NS }, { F_ING, F_PREP, F_TIME, F_MASS, S_NL, F_MASS, F_ING,
S_MD, S_NL, S_A, F_ADJ, F_NS },  { S_A, F_NS, S_NL, F_PREP,
F_TIMED, F_MASS, S_MD, S_NL, F_ADJ }, }; 
#define NFRAMES (sizeof(Frame) / (MAXH * sizeof(FE)))

#define FRAME() (&Frame[rand2() % NFRAMES][0])

#define TOKEN() (rand2() % MAXDICT)
 
#define NCONC(list, word) *list++ = word

#define MEMQ(fe, set) ((fe) & (set))

#define CDR(fe) ((fe) & 255)

#define NOT(p) ((p) == NIL)

#define DPTR(w) (&Dict[w])

#define TPTR(w) (Dict[w].tok)

#define HFE(fes, set) ((fes) & (set))

#define HFES(fes, set) (((fes) & (set)) == (fes))

#define CAT(w, f) (Dict[w].fe & (f))

#define CATS(w, fs) ((Dict[w].fe & (fs)) == (fs))
 
#define REMQ(fe, set) ((~(fe)) & (set))

#define FQ(fe, set) ((fe) | (set))
static char Trigg_check[] = "Trigg!"; 
#define TRIGG_CHECK Trigg_check;
void put16(void *buff, word16 val); word32 rand16(void); word32
rand2(void);  void trigg_solve(byte *link, int diff, byte
*bnum) {  Tdiff = diff; memset(Tchain+32, 0, (256+16)); 
memcpy(Tchain, link, 32); memcpy(Tchain+32+256+16, bnum,
8); put16(link+32, rand16()); put16(link+34, rand16()); 
put16(Tchain+(32+256), rand16()); put16(Tchain+(32+258),
rand16()); }   int trigg_eval(byte *h, byte d) { byte *bp,
n; n = d >> 3; for(bp = h; n; n--) { if(*bp++ != 0) return
NIL; } if((d & 7) == 0) return T; if((*bp & (~(0xff >> (d
& 7)))) != 0) return NIL; return T; }   int trigg_step(byte
*in, int n) { byte *bp; for(bp = in; n; n--, bp++) { bp[0]++; 
if(bp[0] != 0) break; } return T; }  char *trigg_expand(byte
*in, int diff) { int j; byte *bp, *w; DICT *dp; bp = &Tchain[32]; 
memset(bp, 0, 256); for(j = 0; j < 16; j++, in++) { if(*in
== NIL) break; w = TPTR(*in); while(*w) *bp++ = *w++; if(bp[-1]
!= '\n') *bp++ = ' '; } return (char *) &Tchain[32]; }  
byte *trigg_gen(byte *in) { byte *hp; FE *fp; int j, widx; 
fp = FRAME(); hp = in; for(j = 0; j < 16; j++, fp++) { 
if(*fp == NIL) { NCONC(hp, NIL); continue; } if(MEMQ(F_XLIT,
*fp)) { widx = CDR(*fp); } else {  for(;;) { widx = TOKEN(); 
 if(CAT(widx, *fp)) break;  } } NCONC(hp, widx); }  return
in; }   char *trigg_generate(byte *in, int diff) { byte
h[32]; char *cp; SHA256_CTX ctx; trigg_gen(in + 32); trigg_gen(&Tchain[32+256]); 
 cp = trigg_expand(in+32, diff); sha256(Tchain, (32+256+16+8),
h); if(trigg_eval(h, diff) == NIL) {  trigg_step((Tchain+32+256),
16); return NULL; } memcpy(in+(32+16), &Tchain[32+256],
16); return cp; }   int trigg_syntax(byte *in) { FE f[MAXH],
*fp; int j;  for(j = 0; j < MAXH; j++) f[j] = Dict[in[j]].fe; 
 for(fp = &Frame[0][0]; fp < &Frame[NFRAMES][0]; fp += MAXH)
{ for(j = 0; j < MAXH; j++) { if(fp[j] == NIL) { if(f[j]
== NIL) return T; break; } if(MEMQ(F_XLIT, fp[j])) { if(CDR(fp[j])
!= in[j]) break; continue; } if(HFE(f[j], fp[j]) == NIL)
break; } if(j >= MAXH) return T; } return NIL; }  char *trigg_check(byte
*in, byte d, byte *bnum) { byte h[32]; char *cp; SHA256_CTX
ctx;  cp = trigg_expand(in+32, d);  if(trigg_syntax(in+32)
== NIL) return NULL; if(trigg_syntax(in+(32+16)) == NIL)
return NULL;  memcpy(Tchain, in, 32); memcpy((Tchain+32+256),
in+(32+16), 16); memcpy((Tchain+32+256+16), bnum, 8); sha256(Tchain,
(32+256+16+8), h); if(trigg_eval(h, d) == NIL) return NULL; 
return cp; }  