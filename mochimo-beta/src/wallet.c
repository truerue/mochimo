/* wallet.c  Prototype wallet
 *
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 15 March 2018
 * Revised: 30 May 2018
 *
 * NOTE: To compile without a script:
 *
 *       viz. Borland C++ 5.5.
 *       bcc32 -DWIN32 -c sha256.c wots/wots.c
 *       bcc32 -DWIN32 wallet.c wots.obj sha256.obj
 *
 *       Unix-like:
 *       cc -DUNIXLIKE -DLONG64 -c sha256.c wots/wots.c
 *       cc -o wallet -DUNIXLIKE -DLONG64 wallet.c wots.o sha256.o
 *                                   ^
 *                  Remove if your longs are not 64-bit.
 *
 * Disable tag support by adding -DNOADDRTAGS
*/

#include "config.h"
#include "sock.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define VEOK        0      /* No error                    */
#define VERROR      1      /* General error               */
#define VEBAD       2      /* client was bad              */

#ifdef UNIXLIKE
#include <unistd.h>
#define CLEARSCR() system("clear")
#else
#define CLEARSCR() clrscr()
void clrscr(void);
typedef int pid_t;
unsigned sleep(unsigned seconds);
#endif

#define OP_HELLO          1
#define OP_HELLO_ACK      2
#define OP_TX             3
#define OP_GETIPL         6
#define OP_BALANCE        12
#define OP_RESOLVE        14

#define TXNETWORK 0x0539
#define TXEOT     0xabcd
#define TXADDRLEN 2208
#define TXAMOUNT  8
#define TXSIGLEN  2144  /* WOTS */
#define HASHLEN 32
#define TXAMOUNT 8
#define RNDSEEDLEN 64
#define TXBUFF(tx)   ((byte *) tx)
#define TXBUFFLEN    ((2*5) + (8*2) + 32 + 32 + 32 + 2 \
                        + (TXADDRLEN*3) + (TXAMOUNT*3) + TXSIGLEN + (2+2) )
#define TRANBUFF(tx) ((tx)->src_addr)
#define TRANLEN      ( (TXADDRLEN*3) + (TXAMOUNT*3) + TXSIGLEN )
#define SIG_HASH_COUNT (TRANLEN - TXSIGLEN)

#define CRC_BUFF(tx) TXBUFF(tx)
#define CRC_COUNT   (TXBUFFLEN - (2+2))  /* tx buff less crc and trailer */
#define CRC_VAL_PTR(tx)  ((tx)->crc16)

#define ADDR_TAG_PTR(addr) (((byte *) addr) + 2196)
#define ADDR_TAG_LEN 12
#define HAS_TAG(addr) (((byte *) addr)[2196] != 0x42)

#include "sha256.h"      /* also defines word32 */
#include "wots/wots.h"   /* TXADDRLEN */


/* Wallet header */
typedef struct {
   byte sig[4];
   byte lastkey[4];
   byte seed[RNDSEEDLEN];   /* seed to generate random bytes for addresses */
   byte naddr[4];
   byte name[16];
} WHEADER;


/* Wallet entry */
typedef struct {
   byte key[4];
   byte code[1];
   byte amount[8];   /* as computed */
   byte balance[8];  /* as reported */
   byte name[16];
   byte mtime[4];
   byte addr[TXADDRLEN];
   byte secret[32];
} WENTRY;

#define ispending(code) ((code) == 'P' || (code) == 'p')

/* Wallet index entry */
typedef struct {
   byte key[4];
   byte code[1];
   byte amount[8];
   byte balance[8];
   byte name[16];
   byte mtime[4];
   byte hastag[1];
} WINDEX;


/* Multi-byte numbers are little-endian.
 * Structure is checked on start-up for byte-alignment.
 * HASHLEN is checked to be 32.
 */
typedef struct {
   byte version[2];  /* 0x01, 0x00 PVERSION  */
   byte network[2];  /* 0x39, 0x05 TXNETWORK */
   byte id1[2];
   byte id2[2];
   byte opcode[2];
   byte cblock[8];        /* current block num  64-bit */
   byte blocknum[8];      /* block num for I/O in progress */
   byte cblockhash[32];   /* sha-256 hash of our current block */
   byte pblockhash[32];   /* sha-256 hash of our previous block */
   byte weight[32];       /* sum of block difficulties */
   byte len[2];  /* length of data in transaction buffer for I/O op's */
   /* start transaction buffer */
   byte src_addr[TXADDRLEN];
   byte dst_addr[TXADDRLEN];
   byte chg_addr[TXADDRLEN];
   byte send_total[TXAMOUNT];
   byte change_total[TXAMOUNT];
   byte tx_fee[TXAMOUNT];
   byte tx_sig[TXSIGLEN];
   /* end transaction buffer */
   byte crc16[2];
   byte trailer[2];  /* 0xcd, 0xab */
} TX;


/* NODE for rx2() and callserver(): */
typedef struct {
   TX tx;  /* transaction buffer */
   word16 id1;      /* from tx */
   word16 id2;      /* from tx */
   int opcode;      /* from tx */
   word32 src_ip;
   SOCKET sd;
   pid_t pid;     /* process id of child -- zero if empty slot */
} NODE;


word16 get16(void *buff)
{
   return *((word16 *) buff);
}

void put16(void *buff, word16 val)
{
   *((word16 *) buff) = val;
}

/* little-endian */
word32 get32(void *buff)
{
   return *((word32 *) buff);
}

void put32(void *buff, word32 val)
{
   *((word32 *) buff) = val;
}

/* buff<--val */
void put64(void *buff, void *val)
{
   ((word32 *) buff)[0] = ((word32 *) val)[0];
   ((word32 *) buff)[1] = ((word32 *) val)[1];
}

/* Network order word32 as byte a[4] to static alpha string like 127.0.0.1 */
char *ntoa(byte *a)
{
   static char s[24];

   sprintf(s, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
   return s;
}


#include "crc16.c"
#include "rand.c"    /* high speed random number generators */
#include "add64.c"   /* mult-byte maths */
#include "xo4.c"     /* crypto */

void crctx(TX *tx)
{
   put16(CRC_VAL_PTR(tx), crc16(CRC_BUFF(tx), CRC_COUNT));
}


/* bnum is little-endian on disk and core. */
char *bnum2hex(byte *bnum)
{
   static char buff[20];

   sprintf(buff, "%02x%02x%02x%02x%02x%02x%02x%02x",
                  bnum[7],bnum[6],bnum[5],bnum[4],
                  bnum[3],bnum[2],bnum[1],bnum[0]);
   return buff;
}


/* Globals */
#define PASSWLEN 188
char Password[PASSWLEN];
#define WFNAMELEN 256
char Wfname[WFNAMELEN] = "wallet.wal";  /* wallet file name */
XO4CTX Xo4ctx;
WHEADER Whdr;
WINDEX *Windex;
word32 Nindex;
word32 Ndeleted;
word32 Nspent;
char *Tfname;  /* output transaction file name */
byte Needcleanup;    /* for Winsock */
word32 Mfee[2] = { 500, 0 };
byte Zeros[8];
word32 Port = 2095;  /* default server port */
char *Peeraddr;  /* peer address string optional, set on command line */
unsigned Nextcore;  /* index into Coreplist for callserver() */
byte Verbose;       /* output trace messages */
byte Default_tag[ADDR_TAG_LEN]
   = { 0x42, 0, 0, 0, 0x0e, 0, 0, 0, 1, 0, 0, 0 };

#define CORELISTLEN 9

#if CORELISTLEN > RPLISTLEN
#error Fix CORELISTLEN
#endif

/* ip's of the Core Network */
word32 Coreplist[RPLISTLEN] = {
   0x0100007f,    /* local host */
   0xb075ce2f,    /* mdev1 47.206.117.176 */
   0xb175ce2f,    /* mdev2 */
   0xb275ce2f,    /* mdev3 */
   0xb375ce2f,    /* mdev4 */
   0xb475ce2f,    /* mdev5 */
   0xb575ce2f,    /* mdev6 */
   0xb675ce2f,    /* mdev7 */
   0xb775ce2f,    /* mdev8 */
};


byte Sigint;

void ctrlc(int sig)
{
   signal(SIGINT, ctrlc);
   Sigint = 1;
}


/* shuffle a list of < 64k word32's */
void shuffle32(word32 *list, word32 len)
{
   word32 *ptr, *p2, temp;

   if(len < 2) return;
   for(ptr = &list[len - 1]; len > 1; len--, ptr--) {
      p2 = &list[rand16() % len];
      temp = *ptr;
      *ptr = *p2;
      *p2 = temp;
   }
}


/* Search an array list[] of word32's for a non-zero value.
 * A zero value marks the end of list (zero cannot be in the list).
 */
word32 *search32(word32 val, word32 *list, unsigned len)
{
   for( ; len; len--, list++) {
      if(*list == 0) break;
      if(*list == val) return list;
   }
   return NULL;
}


void bytes2hex(byte *addr, int len)
{
   int n;
   
   for(n = 0; len; len--) {
      printf("%02x", *addr++);
      if(++n >= 36) {
         printf("\n");
         n = 0;
      }
   }
   printf("\n");
}


/* Convert nul-terminated hex string in[] to binary out[].
 * in and out may point to same space.
 * example: in[]   = { '0', '1', 'a', '0' }
 *          out[]: = { 1, 160 }
*/
int hex2bytes(char *in, char *out)
{
   char *hp;
   static char hextab[] = "0123456789abcdef";
   int j, len, val = 0;

   len = strlen(in);
   if(len & 1) return 0;  /* len should be even */
   for(j = 0; *in && len; in++, j++, len--) {
      hp = strchr(hextab, tolower(*in));
      if(!hp) break;  /* if non-hex */
      val = (val * 16) + (hp - hextab);  /* convert 4 bits per char */
      if(j & 1) *out++ = val;  /* done with this byte */
   }
   return j;  /* number of characters scanned */
}


/* Display terminal error message
 * and exit.
 */
void fatal(char *fmt, ...)
{
   va_list argp;

   fprintf(stdout, "wallet: ");
   va_start(argp, fmt);
   vfprintf(stdout, fmt, argp);
   va_end(argp);
   printf("\n");
#ifdef _WINSOCKAPI_
    if(Needcleanup)
       WSACleanup();
#endif
   exit(2);
}


#ifdef FIONBIO
/* Set socket sd to non-blocking I/O on Win32 */
int nonblock(SOCKET sd)
{
   u_long arg = 1L;

   return ioctlsocket(sd, FIONBIO, (u_long FAR *) &arg);
}

#else
#include <fcntl.h>

/* Set socket sd to non-blocking I/O
 * Returns -1 on error.
 */
int nonblock(SOCKET sd)
{
   int flags;

   flags = fcntl(sd, F_GETFL, 0);
   return fcntl(sd, F_SETFL, flags | O_NONBLOCK);
}

#endif


int disp_ecode(int ecode)
{
   switch(ecode) {
      case VEOK:    printf("\nOk!\n");        break;
      case VEBAD:   printf("\nBad peer!\n");  break;
      case VERROR:
      default:
                    printf("\n***\n");   break;
   }
   return ecode;
}


int badidx(unsigned idx)
{
   if(idx == 0 || idx > Nindex) {
      printf("\nInvalid index.\n");
      return 1;
   }
   return 0;
}


/* Modified from connect2.c to use printf()
 * Returns: sd = a valid socket number on successful connect, 
 *          else INVALID_SOCKET (-1)
 */
SOCKET connectip2(word32 ip, char *addrstr)
{
   SOCKET sd;
   struct hostent *host;
   struct sockaddr_in addr;
   word16 port;
   char *name;
   time_t timeout;

   if((sd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
bad:
      printf("connectip(): cannot open socket.\n");
      return INVALID_SOCKET;
   }

   port = Port;
   memset((char *) &addr, 0, sizeof(addr));
   name = "";
   if(addrstr) {
      name = addrstr;
      if(addrstr[0] < '0' || addrstr[0] > '9') {
         host = gethostbyname(addrstr);
         if(host == NULL) {
            printf("connectip(): gethostbyname() failed.\n");
            return INVALID_SOCKET;
         }
         memcpy((char *) &(addr.sin_addr.s_addr),
                host->h_addr_list[0], host->h_length);
      }
      else
         addr.sin_addr.s_addr = inet_addr(addrstr);
   } else {
      addr.sin_addr.s_addr = ip;
   }  /* end if NULL addrstr */

   addr.sin_family = AF_INET;  /* AF_UNIX */
   /* Convert short integer to network byte order */
   addr.sin_port = htons(port);

   if(Verbose) {
      if(name[0])
         printf("Trying %s port %d...  ", name, port);
      else
         printf("Trying %s port %d...  ", ntoa((byte *) &ip), port);
   }

   nonblock(sd);
   timeout = time(NULL) + 3;
   Sigint = 0;

retry:
   if(connect(sd, (struct sockaddr *) &addr, sizeof(struct sockaddr))) {
#ifdef WIN32
      errno = WSAGetLastError();
#endif
      if(errno == EISCONN) goto out;
      if(time(NULL) < timeout && Sigint == 0) goto retry;
      closesocket(sd);
      if(Verbose) printf("connectip(): cannot connect() socket.\n");
      return INVALID_SOCKET;
   }
   if(Verbose) printf("connected.");
   nonblock(sd);
out:
   if(Verbose) printf("\n");
   return sd;
}  /* end connectip2() */


/* Send transaction in np->tx */
int sendtx2(NODE *np)
{
   int count;
   TX *tx;

   tx = &np->tx;

   put16(tx->version, PVERSION);
   put16(tx->network, TXNETWORK);
   put16(tx->trailer, TXEOT);
   put16(tx->id1, np->id1);
   put16(tx->id2, np->id2);
   crctx(tx);
   count = send(np->sd, TXBUFF(tx), TXBUFFLEN, 0);
   if(count != TXBUFFLEN)
      return VERROR;
   return VEOK;
}  /* end sendtx2() */


int send_op(NODE *np, int opcode)
{
   put16(np->tx.opcode, opcode);
   return sendtx2(np);
}


/* Receive next packet from NODE *np
 * Returns: VEOK=good, else error code.
 * Check id's if checkids is non-zero.
 * Set checkid to zero during handshake.
 */
int rx2(NODE *np, int checkids)
{
   int count, n;
   time_t timeout;
   TX *tx;

   tx = &np->tx;
   timeout = time(NULL) + 3;

   Sigint = 0;
   for(n = 0; ; ) {
      count = recv(np->sd, TXBUFF(tx) + n, TXBUFFLEN - n, 0);
      if(Sigint) return VERROR;
      if(count == 0) return VERROR;
      if(count < 0) {
         if(time(NULL) >= timeout) return -1;
         continue;
      }
      n += count;
      if(n == TXBUFFLEN) break;
   }  /* end for */

   /* check tx and return error codes or count */
   if(get16(tx->network) != TXNETWORK)
      return 2;
   if(get16(tx->trailer) != TXEOT)
      return 3;
   if(crc16(CRC_BUFF(tx), CRC_COUNT) != get16(tx->crc16))
      return 4;
   if(checkids && (np->id1 != get16(tx->id1) || np->id2 != get16(tx->id2)))
      return 5;
   return VEOK;
}  /* end rx2() */


/* Call peer and complete Three-Way */
int callserver(NODE *np, word32 ip, char *addrstr)
{
   int ecode, j;

   Sigint = 0;

   memset(np, 0, sizeof(NODE));   /* clear structure */
   np->sd = INVALID_SOCKET;
   for(j = 0; j < RPLISTLEN && Sigint == 0; j++) {
      if(Nextcore >= RPLISTLEN) Nextcore = 0;
      ip = Coreplist[Nextcore];
      if(ip == 0 && addrstr == NULL) break;
      np->sd = connectip2(ip, addrstr);
      if(np->sd != INVALID_SOCKET) break;
      if(addrstr == NULL) Nextcore++;
      addrstr = NULL;
   }
   if(np->sd == INVALID_SOCKET) {
      Nextcore = 0;
      return VERROR;
   }
   np->src_ip = ip;
   np->id1 = rand16();
   send_op(np, OP_HELLO);

   ecode = rx2(np, 0);
   if(ecode != VEOK) {
      if(Verbose) printf("*** missing HELLO_ACK packet (%d)\n", ecode);
bad:
      closesocket(np->sd);
      np->sd = INVALID_SOCKET;
      return VERROR;
   }
   np->id2 = get16(np->tx.id2);
   np->opcode = get16(np->tx.opcode);
   if(np->opcode != OP_HELLO_ACK) {
      if(Verbose) printf("*** HELLO_ACK is wrong: %d\n", np->opcode);
      goto bad;
   }
   return VEOK;
}  /* end callserver() */


/* Call with:
 * opcode = OP_BALANCE tx->src_addr is address to query and
 *                     balance returned in tx->send_total.
 *
 * opcode = OP_GETIPL  returns IP list in TRANBUFF(tx) of tx->len bytes.
 *
 * Returns VOEK on success, else VERROR.
 */
int get_tx(TX *tx, word32 ip, char *addrstr, int opcode)
{
   NODE node;

   if(callserver(&node, ip, addrstr) != VEOK)
      return VERROR;
   memcpy(&node.tx, tx, sizeof(TX));
   put16(node.tx.len, 1);  /* signal server that we are a wallet */
   send_op(&node, opcode);
   if(rx2(&node, 1) != VEOK) {
      closesocket(node.sd);
      return VERROR;
   }
   closesocket(node.sd);
   memcpy(tx, &node.tx, sizeof(TX));  /* return tx to caller's space */
   return VEOK;
}  /* end get_tx() */


/* Get a peer list from a Mochimo server at addrstr.
 * Return VEOK or VERROR.
 */
int get_ipl(char *addrstr)
{
   TX tx;
   int status, j, k;
   unsigned len;
   word32 ip, *ipp;

   Sigint = 0;
   memset(&tx, 0, sizeof(TX));
   for(j = 0; j < RPLISTLEN && Sigint == 0; j++) {
      ip = Coreplist[j];
      if(ip == 0 && addrstr == NULL) break;
      status = get_tx(&tx, ip, addrstr, OP_GETIPL);
      len = get16(tx.len);
      /*
       * Insert the peer list after the core ip's in Coreplist[]
       */
      if(status == VEOK && len <= TRANLEN) {
         for(ipp = (word32 *) TRANBUFF(&tx), k = CORELISTLEN;
             k < RPLISTLEN && len > 0;
             ipp++, len -= 4) {
                if(*ipp == 0) continue;
                if(search32(*ipp, Coreplist, k)) continue;
                Coreplist[k++] = *ipp;
         }
         if(k > CORELISTLEN) {
            shuffle32(Coreplist, k);
            Nextcore = 0;
            printf("Addresses added: %d", k - CORELISTLEN);
         }
         return VEOK;
      }  /* end if copy ip list */
      addrstr = NULL;
   }  /* end for try again */
   return VERROR;
}  /* end get_ipl() */


int send_tx(TX *tx, word32 ip, char *addrstr)
{
   NODE node;
   int status;

   if(callserver(&node, ip, addrstr) != VEOK)
      return VERROR;
   memcpy(&node.tx, tx, sizeof(TX));
   put16(node.tx.len, 1);  /* signal server that we are a wallet */
   status = send_op(&node, OP_TX);
   closesocket(node.sd);
   return status;
}  /* end send_tx() */


/* Very special for Shylock: */
void shy_setkey(XO4CTX *ctx, byte *salt, byte *password, unsigned len)
{
   int j;
   byte key[256];

   memset(key, 0, 256);
   if(len > 188) len = 188;   /* 256-64-4 */
   memcpy(&key[64], salt, 4);
   memcpy(&key[68], password, len);
   sha256(&key[64], (256-64), key);
   sha256(key, 256, &key[32]);
   xo4_init(ctx, key, 64);
}

byte *fuzzname(byte *name, int len)
{
   byte *bp;

   for(bp = name; len; len--)
      *bp++ |= ((rand16() & 0x8000) ? 128 : 0);
   return name;
}


byte *unfuzzname(byte *name, int len)
{
   byte *bp;

   for(bp = name; len; len--)
      *bp++ &= 127;
   return name;
}

   
/* Copy outlen random bytes to out.
 * 64-byte seed is incremented.
 */
void rndbytes(byte *out, word32 outlen, byte *seed)
{
   static byte state;
   static byte rnd[PASSWLEN+64];
   byte hash[32];  /* output for sha256() */
   int n;

   if(state == 0) {
      memcpy(rnd, seed, 64);
      memcpy(&rnd[64], Password, PASSWLEN);
      state = 1;
   }
   for( ; outlen; ) {
      /* increment big number in rnd and seed */
      for(n = 0; n < 64; n++) {
         if(++seed[n] != 0) break;
      }       
      for(n = 0; n < 64; n++) {
         if(++rnd[n] != 0) break;
      }       
      sha256(rnd, PASSWLEN+64, hash);
      if(outlen < 32) n = outlen; else n = 32;
      memcpy(out, hash, n);
      out += n;
      outlen -= n;
   }
}  /* end rndbytes() */


/*
 * Create an address that can be later signed with wots_sign():
 * It calls the function wots_pkgen() which creates the address.
*/

/* Make up a random address that can be signed...
 * Outputs:
 *          addr[TXADDRLEN] takes the address (2208 bytes)
 *          secret[32]      needed for wots_sign()
 */
void create_addr(byte *addr, byte *secret, byte *seed)
{
   byte rnd2[32];

   rndbytes(secret, 32, seed);  /* needed later to use wots_sign() */

   rndbytes(addr, TXADDRLEN, seed);
   /* rnd2 is modified by wots_pkgen() */
   memcpy(rnd2, &addr[TXSIGLEN + 32], 32);
   /* generate a good addr */
   wots_pkgen(addr,              /* output first 2144 bytes */
              secret,            /* 32 */
              &addr[TXSIGLEN],   /* rnd1 32 */
              (word32 *) rnd2    /* rnd2 32 (modified) */
   );
   memcpy(&addr[TXSIGLEN+32], rnd2, 32);
}  /* end create_addr() */


char *time2str(word32 time1)
{
  struct tm *tp;
  time_t t;
  static char buff[16];
  static char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  t = time1;
  tp = gmtime(&t);
  if(tp)
     sprintf(buff, "%d %s %d", tp->tm_mday, month[tp->tm_mon],
             tp->tm_year + 1900);
  else
     return "xx-xxx-xxxx";
  return buff;
}


/* Input a string to buff from stdin.
 * len > 2
 */
char *tgets(char *buff, int len)
{
   char *cp, fluff[16];

   *buff = '\0';
   fgets(buff, len, stdin);
   cp = strchr(buff, '\n');
   if(cp) *cp = '\0';
   else {
      for(;;) {
         if(fgets(fluff, 16, stdin) == NULL) break;
         if(strchr(fluff, '\n') != NULL) break;
      }
   }
   return buff;
}


/* Open file, or if fatalflag, call fatal() else return fp. */
FILE *fopen2(char *fname, char *mode, int fatalflag)
{
   FILE *fp;

   fp = fopen(fname, mode);
   if(!fp && fatalflag) fatal("Cannot open %s", fname);
   return fp;
}


FILE *fopen3(char *fname, char *mode, char *safe)
{
   char buff[20];
   FILE *fp;

   fp = fopen(fname, "rb");
   if(fp && safe) {
      fclose(fp);
      printf("%s exists.  %s (y/n): ", fname, safe);
      tgets(buff, 20);
      if(*buff != 'y' && *buff != 'Y') return (FILE *) 1;
   }
   fp = fopen(fname, mode);
   return fp;
}


/* Returns 0 on success, else error code. */
int init_password(void)
{
   char temp[PASSWLEN];
   char temp2[PASSWLEN];

   memset(temp, 0, PASSWLEN);
   memset(temp2, 0, PASSWLEN);

   for(;;) {
      printf("Enter pass phrase of up to %d characters:\n"
             "(You need to remember this.)\n",
             PASSWLEN);
      tgets(temp, PASSWLEN);
      CLEARSCR();
      printf("Re-enter phrase:\n");
      tgets(temp2, PASSWLEN);
      if(strcmp(temp, temp2) == 0) break;
      CLEARSCR();
      printf("Phrases do not match.  Try again (y/n)? ");
      tgets(temp, PASSWLEN);
      if(*temp != 'y' && *temp != 'Y') return 1;  /* Password not changed */
   }
   strcpy(Password, temp);
   CLEARSCR();
   return 0;
}  /* end init_password() */


void init_seed(char *seed, unsigned len)
{
   FILE *fp;
   byte b;

   if(len < 5) return;

   ((word16 *) seed)[0] = rand16();
   ((word16 *) seed)[1] = rand16();

   len -= 4;
   seed += 4;
   printf("Enter a random string of up to %d characters:\n"  
          "(You DO NOT need to remember this.)\n", len);
   tgets(seed, len);
   fp = fopen("/dev/random", "rb");
   if(fp) {
      for( ; len; len--) {
         if(fread(&b, 1, 1, fp) != 1) break;
         *seed++ ^= b;
      }
      fclose(fp);
   }
   CLEARSCR();
}


void init_wallet(WHEADER *wh)
{
   FILE *fp;
   char fname[WFNAMELEN];
   char buff[2], lbuff[100];
   static byte salt[4];  /* salt is always zero for the header */

   memset(wh, 0, sizeof(WHEADER));
   printf("\nPress ctrl-c at any time to cancel.\n\n");

   printf("Enter the name of this wallet: ");
   tgets(lbuff, 100);
   memcpy(wh->name, lbuff, 16);
   if(init_password()) fatal("no pass phrase");
   init_seed((char *) wh->seed, RNDSEEDLEN);
   rndbytes(wh->sig, 4, wh->seed);

get_name:
   printf("Save to file (ctrl-c to quit) [%s]: ", Wfname);
   tgets(fname, WFNAMELEN);
   if(*fname) strncpy(Wfname, fname, WFNAMELEN-1);
   fp = fopen3(Wfname, "wb", "Overwrite");
   if(!fp) fatal("Cannot create %s", Wfname);  /* I/O error */
   if(fp == (FILE *) 1) goto get_name;  /* file existed */

   /* encrypt disk image */
   fuzzname(wh->name, 16);
   shy_setkey(&Xo4ctx, salt, (byte *) Password, PASSWLEN);
   xo4_crypt(&Xo4ctx, wh, wh, sizeof(Whdr));

   if(fwrite(wh, 1, sizeof(WHEADER), fp) != sizeof(WHEADER))
      fatal("error writing %s", Wfname);
   fclose(fp);

   /* decrypt for use */
   shy_setkey(&Xo4ctx, salt, (byte *) Password, PASSWLEN);
   xo4_crypt(&Xo4ctx, wh, wh, sizeof(WHEADER));
   unfuzzname(wh->name, 16);
   printf("Wallet '%s' id: 0x%x created to file %s\n", wh->name,
          *((int *) wh->sig), Wfname);
}  /* end init_wallet() */


char *addr2str(byte *addr)
{
   static char str[10];

   sprintf(str, "%02x%02x%02x%02x", addr[0], addr[1], addr[2], addr[3]);
   return str;
}


void delete_windex(void)
{
   if(Windex) {
      if(Nindex)
         memset(Windex, 0, Nindex * sizeof(WINDEX));  /* security */
      free(Windex);
      Windex = NULL;
   }
   Nindex = Ndeleted = Nspent = 0;
}


int read_wheader(WHEADER *whdr, char *fname)
{
   FILE *fp;
   static byte salt[4];

   /* open and read wallet header */
   fp = fopen2(fname, "rb", 1);
   if(fread(whdr, 1, sizeof(WHEADER), fp) != sizeof(WHEADER))
      fatal("Cannot read %s", fname);
   fclose(fp);
   /* decrypt it now */
   shy_setkey(&Xo4ctx, salt, (byte *) Password, PASSWLEN);
   xo4_crypt(&Xo4ctx, &Whdr, &Whdr, sizeof(WHEADER));
   unfuzzname(Whdr.name, 16);
   printf("Loaded wallet '%s' from %s\n", Whdr.name, fname);
   return 0;
}


int read_wentry(WENTRY *entry, unsigned idx)
{
   FILE *fp;

   if(idx >= Nindex) return VERROR;
   fp = fopen2(Wfname, "rb", 1);
   if(fseek(fp, sizeof(WHEADER) + (idx * sizeof(WENTRY)), SEEK_SET) != 0) {
bad:
      printf("\nI/O error\n");
      fclose(fp);
      return VERROR;
   }
   if(fread(entry, 1, sizeof(WENTRY), fp) != sizeof(WENTRY)) goto bad;
   shy_setkey(&Xo4ctx, entry->key, (byte *) Password, PASSWLEN);
   /* entry->code is first field after entry.key salt */
   xo4_crypt(&Xo4ctx, entry->code, entry->code,
             sizeof(WENTRY) - sizeof(entry->key));
   unfuzzname(entry->name, sizeof(entry->name));
   fclose(fp);
   return VEOK;
}


/* Encrypt and write wallet entry to disk.
 * Returns error code and entry encrypted.
 */
int write_wentry(WENTRY *entry, unsigned idx)
{
   FILE *fp;

   if(idx >= Nindex) return VERROR;
   fp = fopen2(Wfname, "r+b", 1);
   if(fseek(fp, sizeof(WHEADER) + (idx * sizeof(WENTRY)), SEEK_SET) != 0) {
bad:
      printf("\nI/O error\n");
      fclose(fp);
      return VERROR;
   }
   fuzzname(entry->name, sizeof(entry->name));
   shy_setkey(&Xo4ctx, entry->key, (byte *) Password, PASSWLEN);
   /* entry->code is first field after entry.key salt */
   xo4_crypt(&Xo4ctx, entry->code, entry->code,
             sizeof(WENTRY) - sizeof(entry->key));
   if(fwrite(entry, 1, sizeof(WENTRY), fp) != sizeof(WENTRY)) goto bad;
   fclose(fp);
   return VEOK;
}


/* Open and read wallet entries into malloc'd index Windex[]. */
word32 read_widx(char *fname)
{
   FILE *fp;
   long fsize;
   word32 j;
   WINDEX *ip;
   WENTRY entry;

   /* If index already exists, delete it. */
   delete_windex();
   Nindex = Ndeleted = Nspent = 0;

   fp = fopen2(fname, "rb", 1);  /* open file or fatal() */
   fseek(fp, 0, SEEK_END);
   fsize = ftell(fp);   
   fseek(fp, sizeof(WHEADER), SEEK_SET);
   /* check file size */
   if(((fsize - sizeof(WHEADER)) % sizeof(WENTRY)) != 0)
      fatal("Invalid wallet file size on %s", fname);
   /* compute number of address entries in wallet */
   Nindex = (fsize - sizeof(WHEADER)) / sizeof(WENTRY);
   if(Nindex == 0) return Nindex;  /* new file has no entries */

   /* allocate array of WINDEX struct's */
   Windex = malloc(Nindex * sizeof(WINDEX));
   if(!Windex) fatal("No memory to read wallet index!");

   for(j = 0, ip = Windex; j < Nindex; j++, ip++) {
      if(fread(&entry, 1, sizeof(WENTRY), fp) != sizeof(WENTRY)) break;
      shy_setkey(&Xo4ctx, entry.key, (byte *) Password, PASSWLEN);
      /* entry.code is first field after entry.key salt */
      xo4_crypt(&Xo4ctx, entry.code, entry.code,
                sizeof(WENTRY) - sizeof(entry.key));
      unfuzzname(entry.name, sizeof(entry.name));
      memcpy(ip->key, entry.key, sizeof(ip->key));
      ip->code[0] = entry.code[0];
      memcpy(ip->amount, entry.amount, sizeof(ip->amount));
      memcpy(ip->balance, entry.balance, sizeof(ip->balance));
      memcpy(ip->name, entry.name, sizeof(ip->name));
      memcpy(ip->mtime, entry.mtime, sizeof(ip->mtime));
      ip->hastag[0] = 0;
      if(HAS_TAG(entry.addr)) ip->hastag[0] = 1;
      switch(entry.code[0]) {
         case 'D':  Ndeleted++; break;  /* marked for deletion */
         case 'S':  Nspent++;   break;  /* TX sent */
         case 'P':  break;  /* pending transmission */
         case 'p':  break;  /* pending transmission -- spent */
         case 'C':  break;  /* spent confirmed in block chain */
         case 'B':  break;  /* balanced checked */
         case 'I':  break;  /* address imported */
         case 0:    break;
         default:   fatal("Corrupt wallet entry 'code' field: 0x%02x",
                          entry.code[0]);
      }  /* end switch */
   }  /* end for */
   if(j != Nindex || ferror(fp))
      fatal("I/O error reading wallet index");

   if(Ndeleted || Nspent)
      printf("\nDeleted addresses: %d  Spent addresses: %d\n",
             Ndeleted, Nspent);
   fclose(fp);
   memset(&entry, 0, sizeof(WENTRY));  /* security */
   return Nindex;
}  /* end read_widx() */


int ext_addr(unsigned idx)
{
   char buff[80];
   FILE *fp;
   int ecode;
   WENTRY *entry, entryst;

   entry = &entryst;
   if(idx == 0) {
      printf("Export address index (1-%d): ", Nindex);
      tgets(buff, 80);
      idx = atoi(buff);
   }
   if(badidx(idx)) return VERROR;
   ecode = read_wentry(entry, idx-1);
   if(ecode != VEOK) goto out2;

   printf("Write to file name: ");
   tgets(buff, 80);
   ecode = VERROR;
   fp = fopen3(buff, "wb", "Overwrite");
   if(!fp) goto out2;
   if(fp == (FILE *) 1) { ecode = VEOK; goto out2; }  /* user cancelled */
   if(HAS_TAG(entry->addr)) {
      printf("Export tag (y/n)? ");
      tgets(buff, 80);
      if(*buff != 'y' && *buff != 'Y')
         memcpy(ADDR_TAG_PTR(entry->addr), Default_tag, ADDR_TAG_LEN);
   }
   if(fwrite(entry->addr, 1, TXADDRLEN, fp) != TXADDRLEN) goto out;
   printf("Write balance (y/n)? ");
   tgets(buff, 80);
   if(*buff == 'y' || *buff == 'Y') {
      if(fwrite(entry->balance, 1, 8, fp) != 8) goto out;
   } else goto okout;
   printf("Write secret (y/n)? ");
   tgets(buff, 80);
   if(*buff != 'y' && *buff != 'Y') goto okout;
   if(fwrite(entry->secret, 1, 32, fp) != 32) goto out;
okout:
   disp_ecode(VEOK);
   ecode = VEOK;
out:
   fclose(fp);
out2:
   if(ecode != VEOK) printf("\n*** Write error\n");
   memset(entry, 0, sizeof(WENTRY));  /* security */
   return ecode;
}  /* end ext_addr() */


int update_wheader(WHEADER *whdr, char *fname)
{
   FILE *fp;
   static byte salt[4];
   WHEADER whout;

   /* open wallet header */
   fp = fopen2(fname, "r+b", 1);
   /* encrypt it for write to disk */
   memcpy(&whout, whdr, sizeof(WHEADER));
   fuzzname(whout.name, 16);
   shy_setkey(&Xo4ctx, salt, (byte *) Password, PASSWLEN);
   xo4_crypt(&Xo4ctx, &whout, &whout, sizeof(WHEADER));
   if(fwrite(&whout, 1, sizeof(WHEADER), fp) != sizeof(WHEADER))
      fatal("Cannot update wallet header");
   fclose(fp);
   return 0;
}  /* end update_wheader() */


/* Fetch an address with the tag field in addr.
 * Returns VEOK if found with full address in addr, 
 * and balance in balance, else error code.
 */
int get_tag(byte *addr, byte found[1])
{
   int ecode;
   TX tx;

   memset(&tx, 0, sizeof(TX));
   memcpy(tx.dst_addr, addr, TXADDRLEN);
   ecode = get_tx(&tx, 0, Peeraddr, OP_RESOLVE);
   if(ecode != VEOK || get16(tx.opcode) != OP_RESOLVE) {
      found[0] = 0;
      return VERROR;
   }
   memcpy(addr, tx.dst_addr, TXADDRLEN);
   found[0] = tx.send_total[0];  /* 1 if found, else 0 */
   return VEOK;
}  /* end get_tag() */


/* Add address to wallet.  Call after successful read_wheader()
 * Returns index of added address entry.
 */
int add_addr(WENTRY *entry, char *fname, char *name)
{
   FILE *fp;
   word32 lastkey;
   char buff[80];
   long last_idx;
   byte addr[TXADDRLEN];
   byte found;
   int status;
   byte oldtag[ADDR_TAG_LEN];

   fp = fopen2(fname, "ab", 1);  /* open file or fatal() */
   memset(entry, 0, sizeof(WENTRY));
   create_addr(entry->addr, entry->secret, Whdr.seed);

#ifndef NOADDRTAGS
   printf("Add tag (y/n)? ");   /* tag */
   tgets(buff, 80);
   if(*buff == 'y' || *buff == 'Y') {
      ADDR_TAG_PTR(entry->addr)[0] = 0;
      rndbytes(ADDR_TAG_PTR(entry->addr) + 1,
               ADDR_TAG_LEN - 1, Whdr.seed);
   }  /* tag */
#endif  /* tag supported */

   bytes2hex((byte *) entry->addr, 32);
   if(HAS_TAG(entry->addr)) {
      printf("Tag: ");
      bytes2hex(ADDR_TAG_PTR(entry->addr) + 1, ADDR_TAG_LEN - 1);
   }
   lastkey = get32(Whdr.lastkey);  /* salt */
   lastkey++;
   put32(Whdr.lastkey, lastkey);   /* save updated salt */
   put32(entry->key, lastkey);
   put32(entry->mtime, time(NULL)); /* creation time */
   put64(entry->amount, Zeros);     /* initialise address amount */
   memcpy(entry->name, name, sizeof(entry->name));
   fuzzname(entry->name, sizeof(entry->name));
   shy_setkey(&Xo4ctx, entry->key, (byte *) Password, PASSWLEN);
   /* entry->code is first field after entry->key salt */
   xo4_crypt(&Xo4ctx, entry->code, entry->code,
             sizeof(WENTRY) - sizeof(entry->key));
   if(fwrite(entry, 1, sizeof(WENTRY), fp) != sizeof(WENTRY))
      fatal("I/O error");
   last_idx = ftell(fp);
   last_idx = (last_idx - sizeof(WHEADER)) / sizeof(WENTRY);
   fclose(fp);
   update_wheader(&Whdr, Wfname);
   return last_idx;
}  /* end add_addr() */


int add_tag_addr(void)
{
   FILE *fp;
   word32 lastkey;
   char buff[80];
   WENTRY *entry, entst;
   long last_idx;
   byte addr[TXADDRLEN];
   byte found;

#ifdef NOADDRTAGS
   printf("\nTags not supported yet.\n");
   return VERROR;
#else   
   entry = &entst;

   for(;;) {
      printf("Enter tag in hex: ");
      memset(buff, 0, 80);
      tgets(buff, 80);
      if(*buff == '\0') return VERROR;
      if(strlen(buff) == 24) break;
      printf("Please enter 24 hex digits.\n");
   }
   /* Convert buff to binary and */
   /* put buff into dst_addr of dummy TX
    * to query server.
    * If not found, return VERROR, else
    * put the found addr in wallet.
    */
   hex2bytes(buff, buff);
   memcpy(ADDR_TAG_PTR(addr), buff, ADDR_TAG_LEN);
   if(get_tag(addr, &found) != VEOK) {
      printf("\nTag server not connected... Try again.\n");
      Nextcore++;
      return VERROR;
   }      
   if(!found) {
      printf("\nTag not found.\n");
      return VERROR;
   }
   printf("Enter address name: ");
   tgets(buff, 80);

   fp = fopen2(Wfname, "ab", 1);  /* open file or fatal() */
   memset(entry, 0, sizeof(WENTRY));
   strncpy((char *) entry->name, buff, 16);
   memcpy(entry->addr, addr, TXADDRLEN);
   put64(entry->amount, Zeros);
   put64(entry->balance, Zeros);
   lastkey = get32(Whdr.lastkey);  /* salt */
   lastkey++;
   put32(Whdr.lastkey, lastkey);   /* save updated salt */
   put32(entry->key, lastkey);
   put32(entry->mtime, time(NULL)); /* creation time */
   fuzzname(entry->name, sizeof(entry->name));
   shy_setkey(&Xo4ctx, entry->key, (byte *) Password, PASSWLEN);
   /* entry->code is first field after entry->key salt */
   xo4_crypt(&Xo4ctx, entry->code, entry->code,
             sizeof(WENTRY) - sizeof(entry->key));
   if(fwrite(entry, 1, sizeof(WENTRY), fp) != sizeof(WENTRY))
      fatal("I/O error");
   last_idx = ftell(fp);
   last_idx = (last_idx - sizeof(WHEADER)) / sizeof(WENTRY);
   fclose(fp);
   update_wheader(&Whdr, Wfname);
   printf("Address imported.\n");
   return last_idx;
#endif /* tags supported */
}  /* end add_tag_addr() */


/* Add address to wallet.  Call after successful read_wheader()
 * Prompt user.
 */
void add_addr2(char *fname)
{
   WENTRY entry;
   char name[80];
   char buff[40];
   long val;

   printf("Enter address name: ");
   tgets(name, 80);
   if(name[0] == '\0') return;
   if(add_addr(&entry, fname, name) == 0)
      printf("\nAddress NOT created.\n");
   else
      printf("\nAddress '%s' created.\n", name);
   memset(&entry, 0, sizeof(WENTRY));  /* security */
}  /* end add_addr2() */


#define I_ZSUP 1   /* zero suppress */

char *itoa64(void *val64, char *out, int dec, int flags)
{
   int count;
   static char s[24];
   char *cp, zflag = 1;
   word32 *tab;
   byte val[8];

   /* 64-bit little-endian */
   static word32 table[] = {
     0x89e80000, 0x8ac72304,      /* 1e19 */
     0xA7640000, 0x0DE0B6B3,      /* 1e18 */
     0x5D8A0000, 0x01634578,      /* 1e17 */
     0x6FC10000, 0x002386F2,      /* 1e16 */
     0xA4C68000, 0x00038D7E,      /* 1e15 */
     0x107A4000, 0x00005AF3,      /* 1e14 */
     0x4E72A000, 0x00000918,      /* 1e13 */
     0xD4A51000, 0x000000E8,      /* 1e12 */
     0x4876E800, 0x00000017,      /* 1e11 */
     0x540BE400, 0x00000002,      /* 1e10 */
     0x3B9ACA00, 0x00000000,      /* 1e09 */
     0x05F5E100, 0x00000000,      /* 1e08 */
     0x00989680, 0x00000000,      /* 1e07 */
     0x000F4240, 0x00000000,      /* 1e06 */
     0x000186A0, 0x00000000,      /* 1e05 */
     0x00002710, 0x00000000,      /* 1e04 */
     0x000003E8, 0x00000000,      /* 1e03 */
     0x00000064, 0x00000000,      /* 1e02 */
     0x0000000A, 0x00000000,      /* 1e01 */
     0x00000001, 0x00000000,      /*   1  */
   };

   if(out == NULL) cp = s; else cp = out;
   out = cp;  /* return value */
   if((flags & I_ZSUP) == 0) zflag = 0;  /* leading zero suppression flag */
   dec = 20 - (dec + 1);  /* where to put decimal point */
   put64(val, val64);

   for(tab = table; ; ) {
      count = 0;
      for(;;) {
         count++;
         if(sub64(val, tab, val) != 0) {
            count--;
            add64(val, tab, val);
            *cp = count + '0';
            if(*cp == '0' && zflag) *cp = ' '; else zflag = 0;
            cp++;
            if(dec-- == 0) *cp++ = '.';
            tab += 2;
            if(tab[0] == 1 && tab[1] == 0) {
               *cp = val[0] + '0';
               return out;
            }
            break;
         }
      }  /* end for */
   }  /* end for */
}  /* end itoa64() */


/* Convert a decimal ASCII string to a 64-bit value out */
int atoi64(char *string, byte *out)
{
   static byte addin[8];
   static word32 ten[2] = { 10 };
   static word32 tene9[2] = { 1000000000 };  /* Satoshi per Chi */
   int overflow = 0;

   put64(out, Zeros);
   for( ;; string++) {
      if(*string < '0' || *string > '9') {
         if(*string == 'c' || *string == 'C') {
            overflow |= mult64(out, tene9, out);
         }
         return overflow;
      }
      overflow |= mult64(out, ten, out);
      addin[0] = *string - '0';
      overflow |= add64(out, addin, out);  /* add in this digit */
   }  /* end for */
}  /* end atoi64() */


void display_wallet(char *fname, int showspent)
{
   word32 j;
   WINDEX *ip;
   int line;
   char lbuff[10];

   if(!fname) return;
   CLEARSCR();
   read_widx(fname);
   if(Nindex == 0 || Windex == NULL) {
noent:
      printf("No entries.\n");
      return;
   }

   line = 0;
   for(j = 0, ip = Windex; j < Nindex; j++, ip++) {
      if(ip->code[0] == 'D') continue;
      if(!showspent && ip->code[0] == 'S') continue;
      if(showspent && ip->code[0] != 'S') continue;
      if(line == 0)
         printf(
         "\nINDEX       NAME                    AMOUNT            "
         "DATE         STATUS\n");
      if(++line > 19) {  /* page display */
         printf("ENTER=next, q=quit: ");
         tgets(lbuff, 10);
         line = 0;
         if(lbuff[0] == 'q') break;
      }
      if(ispending(ip->code[0]) || ip->code[0] == 'S') {
         printf("%-5d  %-16.16s    (%s)    %-11.11s     %c %c\n",
                j+1,
                ip->name,
                itoa64(ip->amount, NULL, 9, 1),
                time2str(get32(ip->mtime)),
                ip->code[0],
                ip->hastag[0] ? 'T' : ' '
         );
      } else {
         printf("%-5d  %-16.16s      %s    %-11.11s     %c %c\n",
                j+1,
                ip->name,
                itoa64(ip->balance, NULL, 9, 1),
                time2str(get32(ip->mtime)),
                ip->code[0],
                ip->hastag[0] ? 'T' : ' '
         );
      }  /* end if */
   }  /* end for */
   if(line == 0) goto noent;
   if(line > 8) {
      printf("Press return...\n");
      tgets(lbuff, 10);
   }
}  /* end display_wallet() */


int delete_addr(void)
{
   char lbuff[80];
   unsigned idx;
   int ecode;
   WENTRY entry;

   printf("Delete address index (1-%d): ", Nindex);
   tgets(lbuff, 80);
   idx = atoi(lbuff);
   if(badidx(idx)) return VERROR;
   ecode = read_wentry(&entry, idx-1);
   if(ecode != VEOK) return ecode;
   if(cmp64(entry.amount, entry.balance)
      || cmp64(entry.amount, Zeros) != 0) {
         printf("Amounts are not zero.  Are you sure (y/n)? ");
         tgets(lbuff, 80);
         if(lbuff[0] != 'y' && lbuff[0] != 'Y') return VEOK;
   }
   entry.code[0] = 'D';
   ecode = write_wentry(&entry, idx-1);
   return ecode;
}  /* end delete_addr() */


/* Return 1 if tag unusable, else 0. */
int bad_tag(byte *addr)
{
   byte dummy[TXADDRLEN];
   byte found;
   int status;

   memcpy(dummy, addr, ADDR_TAG_LEN);
   Sigint = 0;
   for(;;) {
      printf("\nChecking that change tag is unused.  "
             "Press ctrl-c to cancel...\n");
      status = get_tag(dummy, &found);
      sleep(1);
      if(status != VEOK || Sigint) {
         printf("Tag server not found.  Try again.\n");
         return 1;
      }
      if(found) {
         printf("Tag already in use.  Make new address.\n");
         return 1;
      }
      return 0;
   }
}  /* bad_tag() */


int spend_addr(void)
{
   char buff[128];
   unsigned sidx, didx, cidx;
   int ecode;
   WENTRY sentry, dentry, centry;
   TX tx;
   word32 total[2], change[2];
   byte message[32], rnd2[32];
   byte val[8];
   int status;
   byte found;

top:
   memset(&tx, 0, sizeof(TX));
   memset(&sentry, 0, sizeof(WENTRY));
   memset(&dentry, 0, sizeof(WENTRY));
   memset(&centry, 0, sizeof(WENTRY));
   put64(val, Zeros);
getsrc:
   printf("Spend address index (1-%d, or 0 to cancel): ", Nindex);
   tgets(buff, 80);
   sidx = atoi(buff);
   if(sidx == 0) {
      printf("\nCanceled.\n");
      goto out2;
   }
   if(badidx(sidx)) goto getsrc;
   ecode = read_wentry(&sentry, sidx-1);
   if(ecode != VEOK) {
ioerror:
      printf("\nI/O error.\n");
out2:
       ecode = VERROR;
out:
      memset(&tx, 0, sizeof(TX));          /* clear for security */
      memset(&sentry, 0, sizeof(WENTRY));
      memset(&dentry, 0, sizeof(WENTRY));
      memset(&centry, 0, sizeof(WENTRY));
      return ecode;
   }  /* end if error */

   if(ispending(sentry.code[0])) {
pending:
      printf("That address is pending\n");
      goto top;
   }
   if(sentry.code[0] != 'B') {
      printf("\nYou should check balance %d first.\n", sidx);
      goto out2;
   }
   sentry.code[0] = 'p';
   memcpy(tx.src_addr, sentry.addr, TXADDRLEN);

getdst:
   printf("Destination address index (1-%d, or 0 for none): ", Nindex);
   tgets(buff, 80);
   didx = atoi(buff);
   if(didx == 0) goto skipdst;
   if(didx > Nindex) goto getdst;
   if(didx == sidx) goto getdst;
   ecode = read_wentry(&dentry, didx-1);
   if(dentry.code[0] == 'S') {
addrspent:
      printf("That address is already spent.\n");
      goto top;
   }
   if(ispending(dentry.code[0])) goto pending;
   if(ecode != VEOK) goto ioerror;
getamt:
   printf("Enter send amount in Satoshi (or append c for Chi):\n");
   tgets(buff, 80);
   if(atoi64(buff, val)) {
      printf("Overflow.  Try again.\n");
      goto getamt;
   }
skipdst:
   printf("You entered: %s\n", itoa64(val, NULL, 9, 1));
   put64(tx.send_total, val);
   memcpy(tx.dst_addr, dentry.addr, TXADDRLEN);
   add64(dentry.amount, tx.send_total, dentry.amount);
   dentry.code[0] = 'P';

getchg:
   printf("Change address index (1-%d, or 0 to cancel): ", Nindex);
   tgets(buff, 80);
   cidx = atoi(buff);
   if(cidx == 0) goto out2;
   if(cidx > Nindex) goto getchg;
   if(cidx == sidx || cidx == didx) goto getchg;
   ecode = read_wentry(&centry, cidx-1);
   if(ecode != VEOK) goto ioerror;
   if(centry.code[0] == 'S') goto addrspent;
   if(ispending(centry.code[0])) goto pending;
   memcpy(tx.chg_addr, centry.addr, TXADDRLEN);
   /* Calculate change and check source funds. */
   add64(Mfee, tx.send_total, total);
   if(cmp64(total, sentry.balance) > 0) {
nofunds:
      printf("\nInsufficient funds.\n");
      goto out2;
   }
   if(sub64(sentry.balance, total, change) != 0) goto nofunds;
   put64(tx.change_total, change);
   add64(centry.amount, change, centry.amount);
   memcpy(tx.chg_addr, centry.addr, TXADDRLEN);
   put64(tx.tx_fee, Mfee);
   centry.code[0] = 'P';

   if(HAS_TAG(tx.chg_addr))
      printf("Change address has tag.\n");
   if(HAS_TAG(tx.src_addr)) {
      printf("Source address has tag.\n");
      memcpy(ADDR_TAG_PTR(tx.chg_addr),
             ADDR_TAG_PTR(tx.src_addr), ADDR_TAG_LEN);
      memcpy(ADDR_TAG_PTR(centry.addr),
             ADDR_TAG_PTR(tx.chg_addr), ADDR_TAG_LEN);
   }

   if(memcmp(tx.src_addr, tx.dst_addr, TXADDRLEN - ADDR_TAG_LEN) == 0) {
      printf("\nFrom and to address are the same.\n");
badd:
      printf("Create a new address.\n");
      goto out2;
   }
   if(memcmp(tx.src_addr, tx.chg_addr, TXADDRLEN - ADDR_TAG_LEN) == 0) {
      printf("\nFrom and change address are the same.\n");
      goto badd;
   }
   if(memcmp(tx.dst_addr, tx.chg_addr, TXADDRLEN - ADDR_TAG_LEN) == 0) {
      printf("\nDestination and change address are the same.\n");
      goto badd;
   }

   /* hash tx to message*/
   sha256(tx.src_addr,  SIG_HASH_COUNT, message);
   /* sign TX with secret key for src_addr*/
   memcpy(rnd2, &tx.src_addr[TXSIGLEN+32], 32);  /* temp for wots_sign() */
   wots_sign(tx.tx_sig,  /* output 2144 */
             message,    /* hash 32 */
             sentry.secret,     /* random secret key 32 */
             &tx.src_addr[TXSIGLEN],    /* rnd1 32 */
             (word32 *) rnd2            /* rnd2 32 (maybe modified) */
   );

   if(get32(Mfee) < 100000)
      printf("Transaction fee is 0.0000%05u Chi\n", get32(Mfee));
   printf("Confirm send transaction (y/n)? ");
   tgets(buff, 80);
   if(buff[0] != 'y' && buff[0] != 'Y') {
notsent:
      printf("*** Not sent.\n");
      goto out2;
   }
   if(HAS_TAG(tx.chg_addr) && bad_tag(tx.chg_addr)) goto notsent;

   /* write TX to console and/or transmit */
   bytes2hex((byte *) &tx, sizeof(TX));
   printf("Trying connection.  Press ctrl-c to stop...\n");
   ecode = send_tx(&tx, 0, Peeraddr);
   if(ecode == VEOK)
      printf("Sent!\n");
   else {
      printf("*** Host not found\n");
      goto out2;
   }
   put64(sentry.balance, tx.send_total);
   ecode = write_wentry(&sentry, sidx-1);   /* update wallet */
   if(didx)
      ecode |= write_wentry(&dentry, didx-1);  /* update wallet */
   ecode |= write_wentry(&centry, cidx-1);  /* update wallet */
   if(ecode != VEOK) goto ioerror;
   goto out;
}  /* end spend_addr(void) */


int import_addr(void)
{
   char buff[80];
   FILE *fp;
   int ecode;
   WENTRY *entry, entryst;
   unsigned idx;

   entry = &entryst;
   printf("Enter address name: ");
   tgets(buff, 80);
   if(*buff == '\0') return VERROR;
   idx = add_addr(entry, Wfname, buff);
   if(idx == 0) return VERROR;
   read_widx(Wfname);
   if(badidx(idx)) return VERROR;
   ecode = read_wentry(entry, idx-1);
   if(ecode != VEOK) goto out2;

   ecode = VERROR;
   printf("Import file name: ");
   tgets(buff, 80);
   if(buff[0] == '\0') goto out2;
   fp = fopen(buff, "rb");
   if(!fp) {
      printf("Cannot open %s\n", buff);
      goto out2;
   }
   if(fread(entry->addr, 1, TXADDRLEN, fp) != TXADDRLEN) {
      printf("Cannot read address\n");
      goto out;
   }
   if(fread(entry->amount, 1, 8, fp) != 8) {
      ecode = VEOK;
      goto out;
   }
   printf("Import secret (y/n)? ");
   tgets(buff, 80);
   if(*buff != 'y' && *buff != 'Y') goto okout;
   if(fread(entry->secret, 1, 32, fp) != 32) goto out;
okout:
   disp_ecode(VEOK);
   ecode = VEOK;
out:
   fclose(fp);
out2:
   if(ecode == VEOK) {
      entry->code[0] = 'I';
      put32(entry->mtime, time(NULL));  /* modification time */
      ecode = write_wentry(entry, idx-1);   /* update wallet */
      printf("Address imported.\n");
   }
   if(ecode != VEOK) printf("*** Not imported\n");
   memset(entry, 0, sizeof(WENTRY));  /* security */
   return ecode;
}  /* end import_addr() */


int check_bal(unsigned idx, int resetpend)
{
   char lbuff[10];
   int ecode;
   WENTRY entry;
   WINDEX *ip;
   TX tx;

   if(badidx(idx)) return VERROR;
   ecode = read_wentry(&entry, idx-1);
   if(ecode != VEOK) goto out;
   memset(&tx, 0, sizeof(TX));
   memcpy(tx.src_addr, entry.addr, TXADDRLEN);
   tx.send_total[0] = 1;
   ecode = get_tx(&tx, 0, Peeraddr, OP_BALANCE);
   if(ecode != VEOK) goto out;
   ip = &Windex[idx-1];
   if(entry.code[0] == 'p' && cmp64(tx.send_total, Zeros) == 0) {
      put64(entry.amount, entry.balance);
      put64(ip->amount, entry.balance);
   }
   put64(ip->balance, tx.send_total);
   put64(entry.balance, tx.send_total);
   if(ispending(entry.code[0]) && resetpend == 0)  {
      put32(entry.mtime, time(NULL));  /* modification time */
      /* mark pending transactions spent */
      if(cmp64(entry.balance, Zeros) == 0)
         entry.code[0] = 'S';
      else
         entry.code[0] = 'B';
   }
   else {
      if(entry.code[0] == 'S' || entry.code[0] == 'D') {
         if(cmp64(entry.balance, Zeros) != 0) {
            entry.code[0] = 'B';
            put32(entry.mtime, time(NULL));
         }
      } else {
         put32(entry.mtime, time(NULL));
         entry.code[0] = 'B';
      }
   }
   ecode = write_wentry(&entry, idx-1);
   printf("Balance of %-16.16s: %s\nBlock: 0x%s\n", ip->name,
          itoa64(ip->balance, NULL, 9, 1), bnum2hex(tx.cblock));
out:
   if(ecode != VEOK)
      printf("*** communication error\n");
   memset(&tx, 0, sizeof(TX));         /* security */
   memset(&entry, 0, sizeof(WENTRY));
   return ecode;
}  /* end check_bal() */


int check_bal2(int promptf)
{
   unsigned idx, j;
   char lbuff[40];
   int ecode;
   WINDEX *ip;

   idx = 0;
   if(promptf) {
      printf("Query Balance index (1-%d or 0 for all pending): ", Nindex);
      tgets(lbuff, 40);
      idx = atoi(lbuff);
   }
   if(idx == 0) {
      ecode = VEOK;
      for(ip = Windex, j = 1; j <= Nindex; ip++, j++) {
         if(ispending(ip->code[0]))
            ecode |= check_bal(j, 0);
      }
      return ecode;
   }  /* end if idx == 0 */
   if(badidx(idx)) return VERROR;
   return check_bal(idx, 0);
}  /* end check_bal2() */


/* Reset pending balances */
int reset_pend(void)
{
   unsigned j;
   int ecode;
   WINDEX *ip;

   Sigint = 0;

   ecode = VEOK;
   for(ip = Windex, j = 1; j <= Nindex; ip++, j++) {
      if(Sigint) break;
      if(ispending(ip->code[0]))
         ecode = check_bal(j, 1);
   }
   return ecode;
}  /* end reset_pend() */


int query_all(void)
{
   unsigned j;
   int ecode;
   WINDEX *ip;

   Sigint = 0;

   ecode = VEOK;
   for(ip = Windex, j = 1; j <= Nindex; ip++, j++) {
      if(Sigint) break;
      ecode = check_bal(j, 0);
   }
   return ecode;
}  /* end query_all(() */


int edit_name(void)
{
   char lbuff[100];
   unsigned idx;
   int ecode;
   WENTRY entry;

   printf("Change address name.\nindex (1-%d): ", Nindex);
   tgets(lbuff, 100);
   idx = atoi(lbuff);
   if(badidx(idx)) return VERROR;
   ecode = read_wentry(&entry, idx-1);
   if(ecode != VEOK) return ecode;
   printf("%-16.16s\nEnter new name or press ENTER to cancel:\n",
          entry.name);
   tgets(lbuff, 100);
   if(lbuff[0])
      memcpy(entry.name, lbuff, 16);
   ecode = write_wentry(&entry, idx-1);
   if(ecode == VEOK)
      disp_ecode(VEOK);
   return ecode;
}  /* end edit_name() */


void setup(void)
{
   printf("\nSettings not implemented yet...\n");
}

void get_peers(char *peeraddr)
{
   int status;

   printf("Press ctrl-c to stop...\n");
   status = get_ipl(peeraddr);
   if(Sigint) return;
   disp_ecode(status); 
}


int display_hex(void)
{
   char lbuff[100];
   unsigned idx;
   int ecode;
   WENTRY entry;

   printf("Display address in hexadecimal.\nindex (1-%d): ", Nindex);
   tgets(lbuff, 100);
   idx = atoi(lbuff);
   if(badidx(idx)) return VERROR;
   ecode = read_wentry(&entry, idx-1);
   if(ecode != VEOK) return ecode;
   printf("\n%-16.16s\n", entry.name);
   bytes2hex(entry.addr, 576);
   printf("(more)\nTag: ");
   bytes2hex(ADDR_TAG_PTR(entry.addr), ADDR_TAG_LEN);
   printf("Press return...");
   tgets(lbuff, 100);
   return VEOK;
}  /* end display_hex() */


int remove_tag(void)
{
   int ecode;
   WENTRY entry;
   unsigned idx;
   char lbuff[80];

   printf("Remove address tag index (1-%d): ", Nindex);
   tgets(lbuff, 80);
   idx = atoi(lbuff);
   if(badidx(idx)) return VERROR;
   ecode = read_wentry(&entry, idx-1);
   if(ecode != VEOK) goto out;
   memcpy(ADDR_TAG_PTR(entry.addr), Default_tag, ADDR_TAG_LEN);
   put32(entry.mtime, time(NULL));  /* modification time */
   ecode = write_wentry(&entry, idx-1);
out:
   if(ecode != VEOK)
      printf("*** I/O error\n");
   else
      printf("Tag removed.\n");
   memset(&entry, 0, sizeof(WENTRY));  /* security */
   return ecode;
}  /* end remove_tag() */


int menu2(void)
{
   char buff[20];

   CLEARSCR();
   printf("\nWallet version 0.25\n\n");

   for( ;; ) {
      printf("\n          Menu 2\n\n"
             "  1. Reset pending balances\n"
             "  2. Query all address balances\n"
             "  3. Edit address name\n"
             "  4. Get a Mochimo peer list\n"
             "  5. Display spent addresses\n"
             "  6. Display address in hex\n"
             "  7. Import tagged address\n"
             "  8. Remove address tag\n"
             "  9. Main Menu\n"
             "  0. Exit\n\n"
             "  Select: "
      );
      tgets(buff, 20);
      switch(buff[0]) {
         case '1':  reset_pend();  break;
         case '2':  query_all();   break;
         case '3':  edit_name();   break;
         case '4':  get_peers(Peeraddr);  break;
         case '5':  display_wallet(Wfname, 1);      break;
         case '6':  display_hex(); break;
         case '7':  add_tag_addr();
                    read_widx(Wfname);  break;
         case '8':  remove_tag();       break;
         case '9': return -1;  /* previous menu */
         case '0': return 0;   /* exit */
      }  /* end switch */
   }  /* end for */
}  /* end menu2(); */


void mainmenu(void)
{
   char buff[20];

   printf("\nWallet version 0.25\n\n");
   read_widx(Wfname);

   signal(SIGINT, ctrlc);
   printf("Fetching peer list\n");
   get_peers(Peeraddr);   /* get IPL on start-up */
   printf("Checking balances, press ctrl-c to stop...\n");
   query_all();  /* check all old balances */
   display_wallet(Wfname, 0);

   for( ;; ) {
      printf("\n          Main Menu\n\n"
         "  1. Settings         2. Display          3. Import address\n"
         "  4. Create address   5. Spend address    6. Check balances\n"
         "  7. Export address   8. Delete address   9. Menu 2\n"
         "  0. Exit\n\n"
         "  Select: "
      );
      tgets(buff, 20);
      switch(buff[0]) {
         case '1': setup();                break;
         case '2': display_wallet(Wfname, 0);  break;
         case '3': import_addr();  /* with create */
                   read_widx(Wfname);      break;
         case '4': add_addr2(Wfname);
                   read_widx(Wfname);      break;
         case '5': spend_addr();
                   read_widx(Wfname);
                   break;
         case '6': check_bal2(1);  break;  /* with prompt */
         case '7': ext_addr(0);            break;
         case '8': delete_addr();          break;
         case '9': if(menu2() == 0) return;
                   break;
         case '0': return;
      }  /* end switch */
   }  /* end for */
}  /* end mainmenu(); */


void usage(void)
{
   printf("\nUsage: wallet [-option -option2 . . .] [wallet_file]\n"
      "options:\n"
      "           -aS set address string to S\n"
      "           -pN set TCP port to N\n"
      "           -v  verbose output\n\n"
      "           -n  create new wallet\n\n"
   );
   exit(1);
}


int main(int argc, char **argv)
{
   int j;
   static byte newflag;

#ifdef _WINSOCKAPI_
   static WORD wsaVerReq;
   static WSADATA wsaData;

   wsaVerReq = 0x0101;	/* version 1.1 */
   if(WSAStartup(wsaVerReq, &wsaData) == SOCKET_ERROR)
      fatal("WSAStartup()");
   Needcleanup = 1;
#endif

   for(j = 1; j < argc; j++) {
      if(argv[j][0] != '-') break;
      switch(argv[j][1]) {
         case 'p':  Port = atoi(&argv[j][2]);   /* TCP port */
                    break;
         case 'a':  if(argv[j][2]) Peeraddr = &argv[j][2];
                    break;
         case 'v':  Verbose = 1;
                    break;
         case 'n':  newflag = 1;
                    break;
         default:   usage();
      }  /* end switch */
   }  /* end for j */

   srand16(time(NULL));

#ifndef DEBUG
   shuffle32(Coreplist, CORELISTLEN);
#endif

   if(newflag) init_wallet(&Whdr);
   else if(argv[j]) {
      strncpy(Wfname, argv[j], WFNAMELEN-1);
      printf("Password:\n");
      tgets(Password, PASSWLEN);
      CLEARSCR();
      read_wheader(&Whdr, Wfname);
      printf("Press RETURN to continue or ctrl-c to cancel...\n");
      getchar();
      mainmenu();
   } else usage();

   delete_windex();
   memset(&Whdr, 0, sizeof(Whdr));

#ifdef _WINSOCKAPI_
    if(Needcleanup)
       WSACleanup();
#endif

   return 0;
}
