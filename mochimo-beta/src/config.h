/* config.h  Mochimo configuration and types.
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 1 January 2018
 *
 * NOTE: Edit the configuration section for your compiler and machine.
*/

#ifndef _CONFIG_H
#define _CONFIG_H

/* --------------- configuration section ------------------*/

/* Uncomment next line in you have a Unix-like system. */
/* #define UNIXLIKE */

/* #define WIN32 */

/* Uncomment next line if your long is 64-bit type (check <stdint.h>) */
/* #define LONG64 */

/* Uncomment SWAPBYTES line to swap bytes if your machine is big-endian.
 * NOTE: big-endian machines also need LONG64
 */
/* #define SWAPBYTES */

/* ------ end configuration section -----*/


/* ------ Begin Dev Section -----*/

/* Uncomment next line if you want debug version. */
/* #define DEBUG */

/* Version checking */
#ifndef PVERSION
#define PVERSION      1      /* protocol version number (short) */
#endif

/* Adjustable Parameters */
#define MAXNODES      37       /* maximum number of connected nodes  */
#define LQLEN         100      /* listen() queue length              */
#define INIT_TIMEOUT  3        /* initial timeout after accept()     */
#define ACK_TIMEOUT   10       /* timeout in callserver()            */
#define TXQUEBIG      32       /* big enough to run bcon             */
#define MAXBLTX       32768    /* max TX's in a block for bcon (~1M) */
#define STATUSFREQ    10       /* status display interval sec.       */
#define BCDIR         "bc"     /* rename to dir for block storage    */
#define CPINKLEN     100       /* maximum entries in pinklists       */
#define LPINKLEN     100
#define EPINKLEN     100
#define EPOCHMASK     15       /* update pinklist Epoch count - 1    */
#define EPOCHSHIFT    4
#define RPLISTLEN     200      /* recent peer list */
#define CPLISTLEN     8        /* current peer list */
#define CRCLISTLEN    1024     /* recent tx crc's */
#define LULL          30       /* seconds between doubt */
#define MAXQUORUM     8        /* for get_eon() gang[] */

#ifdef DEBUG
/* was 15, 7, and 5 in v.22 */
#define HIGHSOLVE  6
#define LOWSOLVE   3
#define BCONFREQ   5
#else
#define HIGHSOLVE  506
#define LOWSOLVE   253
#define BCONFREQ   30  /* run bcon at least sec. */
#endif

/* ------ end Dev Section  -----*/

/* ----------------- DO NOT CHANGE BELOW THIS LINE --------------------- */
#define PORT1 2095   /* main TCP listening port */
#define PORT2 2096   /* secondary port */
#ifndef DSTPORT
#define DSTPORT Dstport
#endif

#define HASHLEN 32

#define DEVNULL "/dev/null"
#define SORTLTCMD()  system("../sortlt ltran.dat")

#ifndef WORD32
#define WORD32
typedef unsigned char byte;      /* 8-bit byte */
typedef unsigned short word16;   /* 16-bit word */
typedef unsigned int word32;     /* 32-bit word  */
/* for 16-bit machines: */
/* typedef unsigned long word32;  */
#endif  /* WORD32 */

#ifdef LONG64
typedef unsigned long word64;
#endif  /* LONG64 */

#endif   /* _CONFIG_H */
