/* util.c  Support functions
 *
 * Copyright (c) 2018 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.TXT   **** NO WARRANTY ****
 *
 * Date: 2 January 2018
 *
 * TCP support code.
*/


void swap32(void *val)
{
   byte t, *bp;

   bp = (byte *) val;
   t = bp[0]; bp[0] = bp[3]; bp[3] = t;
   t = bp[1]; bp[1] = bp[2]; bp[2] = t;
}

void swap64(void *val)
{
   byte t, *bp;

   bp = (byte *) val;
   t = bp[0]; bp[0] = bp[7]; bp[7] = t;
   t = bp[1]; bp[1] = bp[6]; bp[6] = t;
   t = bp[2]; bp[2] = bp[5]; bp[5] = t;
   t = bp[3]; bp[3] = bp[4]; bp[4] = t;
}


#ifndef SWAPBYTES
/* little-endian compiler order */

word16 get16(void *buff)
{
   return *((word16 *) buff);
}

void put16(void *buff, word16 val)
{
   *((word16 *) buff) = val;
}

long getseekval(void *buff)
{
   return *((long *) buff);
}

void putseekval(void *buff, long val)
{
   *((long *) buff) = val;
}

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


#else
/* big-endian */

word16 get16(void *buff)
{
   word16 val;

   ((byte *) &val)[0] = buff[1];
   ((byte *) &val)[1] = buff[0];
   return val;
}

void put16(void *buff, word16 val)
{
   buff[0] = ((byte *) &val)[1];
   buff[1] = ((byte *) &val)[0];
}

#ifdef LONG64
/* for 8-byte longs */
long getseekval(byte *buff)
{
   long val;

   ((byte *) &val)[0] = buff[3];
   ((byte *) &val)[1] = buff[2];
   ((byte *) &val)[2] = buff[1];
   ((byte *) &val)[3] = buff[0];
   return val;
}

void putseekval(byte *buff, long val)
{
   buff[0] = ((byte *) &val)[3];
   buff[1] = ((byte *) &val)[2];
   buff[2] = ((byte *) &val)[1];
   buff[3] = ((byte *) &val)[0];
}
#endif /* LONG64 */

#endif  /* SWAPBYTES (big endian) */


/* Network order word32 as byte a[4] to static alpha string like 127.0.0.1 */
char *ntoa(byte *a)
{
   static char s[24];

   sprintf(s, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
   return s;
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


/* Remove bad from list[maxlen]
 * Returns: 0 if bad is not in list.
 *          bad if removed.
 *
 * NOTE: Can change order of list.
*/
word32 remove32(word32 bad, word32 *list, unsigned maxlen)
{
   word32 *bp, *good;

   bp = search32(bad, list, maxlen);
   if(bp == NULL) return 0;
   for(good = &list[maxlen - 1]; good > list && *good == 0; ) good--;
   *bp = *good;
   *good = 0;
   return bad;
}


/* shuffle a list of < 64k word32's using Durstenfeld's algorithm */
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


#ifndef EXCLUDE_NODES

#define recentip(ip) search32(ip, Rplist, RPLISTLEN)
#define currentip(ip) search32(ip, Cplist, CPLISTLEN)
#define recentcrc(crc) search32(crc, Crclist, CRCLISTLEN)

void addrecent(word32 ip)
{
   if(ip == 0) return;
   if(search32(ip, Rplist, RPLISTLEN) != NULL) return;
   if(Rplistidx >= RPLISTLEN) Rplistidx = 0;
   Rplist[Rplistidx++] = ip;
}

void addcurrent(word32 ip)
{
   if(ip == 0) return;
   if(search32(ip, Cplist, CPLISTLEN) != NULL) return;
   if(Cplistidx >= CPLISTLEN) Cplistidx = 0;
   Cplist[Cplistidx++] = ip;
}


void addtxcrc(word32 crc)
{
   if(crc == 0) return;
   if(Crclistidx >= CRCLISTLEN) Crclistidx = 0;
   Crclist[Crclistidx++] = crc;
}


/*
 * Save Rplist[] list to disk.
 */
int save_rplist(void)
{
   int j;

   if(Trace) plog("saving recent peer list...");

   /* save non-zero entries */
   for(j = 0; j < RPLISTLEN; j++)
      if(Rplist[j] == 0) break;

   write_data(Rplist, j * 4, "rplist.lst");
   return VEOK;
}  /* end save_rplist() */


/* Thanks David! */
int existsnz(char *fname)
{
   FILE *fp;
   long len;

   fp = fopen(fname, "rb");
   if(!fp) return 0;
   fseek(fp, 0, SEEK_END);
   len = ftell(fp);
   fclose(fp);
   if(len == 0) return 0;
   return 1;
}

#endif  /* !EXCLUDE_NODES */


int exists(char *fname)
{
   FILE *fp;

   fp = fopen(fname, "rb");
   if(!fp) return 0;
   fclose(fp);
   return 1;
}


/* Returns VEOK or
 * or error code.
 */
int write_data(void *buff, int len, char *fname)
{
   FILE *fp;
   int count;

   fp = fopen(fname, "wb");
   if(!fp) {
bad:
      return error("write_data(): cannot write %s", fname);
   }
   count = 0;
   if(len) count = fwrite(buff, 1, len, fp);
   fclose(fp);
   if(count != len) goto bad;
   return VEOK;
}


/* Returns read count or -1 if fopen() error */
int read_data(void *buff, int len, char *fname)
{
   FILE *fp;
   int count;

   if(len == 0) return 0;
   fp = fopen(fname, "rb");
   if(fp == NULL) return 0;
   count = fread(buff, 1, len, fp);
   fclose(fp);
   return count;
}


/* Seek to end of fname and read block trailer.
 * Return VEOK on success, else error code.
 */
int readtrailer(BTRAILER *trailer, char *fname)
{
   FILE *fp;

   fp = fopen(fname, "rb");
   if(!fp) {
bad:
      return error("Cannot read block trailer from %s", fname);
   }
   if(fseek(fp, -(sizeof(BTRAILER)), SEEK_END) != 0) {
      fclose(fp);
      goto bad;
   }
   if(fread(trailer, 1, sizeof(BTRAILER), fp) != sizeof(BTRAILER)) {
      fclose(fp);
      goto bad;
   }
   fclose(fp);
   return VEOK;
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


char *addr2str(byte *addr)
{
   static char str[10];

   sprintf(str, "%02x%02x%02x%02x", addr[0], addr[1], addr[2], addr[3]);
   return str;
}


/* Return static printable string of hash[HASHLEN] input */
char *hash2str(byte *hash)
{
   static char s[(HASHLEN*2)+4];
   int n;
   char *cp;

   for(cp = s, n = 0; n < HASHLEN; n++, cp += 2)
      sprintf(cp, "%02x", *hash++);
   return s;
}


int moveublock(char *ublock, byte *newnum)
{
   char buff[256];
   char cmd[256];
   char *bnum;

   bnum = bnum2hex(newnum);
   sprintf(buff, "b%s.bc", bnum);
   sprintf(cmd, "%s/b%s.bc", Bcdir, bnum);
   if(exists(buff) || exists(cmd)) {
      error("%s already exists!", buff);
bad:
      error("cannot move block %s", buff);
      return VERROR;
   }
   if(rename(ublock, buff) != 0) {
      error("moveublock(): rename() failed");
      goto bad;
   }
   sprintf(cmd, "mv %s %s", buff, Bcdir);
   system(cmd);
   sprintf(buff, "%s/b%s.bc", Bcdir, bnum);
   if(!exists(buff)) {
      error("moveublock(): system(%s) failed", cmd);
      goto bad;
   }
   return VEOK;
}


/* Check if buff is all zeros */
int iszero(void *buff, int len)
{
   byte *bp;

   for(bp = buff; len; bp++, len--)
      if(*bp) return 0;

   return 1;
}


/* Read a list of 32-bit words from a file
 * Zeros list first.
 * Returns error code.
 */
int readlist32(word32 *list, unsigned size, unsigned maxlen, char *fname,
  word32 *tailptr)
{
   FILE *fp;
   unsigned j;

   memset(list, 0, size * maxlen);
   fp = fopen(fname, "rb");
   if(fp) {
      fread(list, size, maxlen, fp);  /* count = ... */

/* Allow short lists
      if(count != maxlen) {
         error("read error on %s", fname);
         fclose(fp);
         return VERROR;
      }
*/
      fclose(fp);
   }  /* end if fp -- file exists */
   if(tailptr != NULL) {
      /* set j to 0 or index of first zero element in list[] */
      for(j = 0; j < maxlen && list[j] != 0; j++);
      if(j >= maxlen) j = 0;
      *tailptr = j;
   }
   return VEOK;
}  /* end readlist32() */


/* Read in common global data */
int read_global(void)
{
   FILE *fp;
   int count;

   fp = fopen("global.dat", "rb");
   if(!fp) {
      error("Cannot read global.dat");
      return VERROR;
   }
   count = 0;
   count += fread(Cblocknum,    1,  8, fp);
   count += fread(Cblockhash,   1, 32, fp);
   count += fread(Prevhash,     1, 32, fp);
   count += fread(&Peerip,      1,  4, fp);
   count += fread(&Errorlog,    1,  1, fp);
   count += fread(&Trace,       1,  4, fp);
   count += fread(&Mfee,        1,  8, fp);
   count += fread(&Difficulty,  1,  4, fp);
   count += fread(&Time0,       1,  4, fp);
   if(count != (8+32+32+4+1+4+8+4+4)) {
      fclose(fp);
      error("I/O error on global.dat");
      return VERROR;
   }
   fclose(fp);
   return VEOK;
}  /* end read_global() */


/* Write out common global data */
int write_global(void)
{
   FILE *fp;
   int count;

   fp = fopen("global.dat", "wb");
   if(!fp) {
bad:
      return error("write_global() bad I/O on global.dat");
   }

   count = 0;
   count += fwrite(Cblocknum,    1,  8, fp);
   count += fwrite(Cblockhash,   1, 32, fp);
   count += fwrite(Prevhash,     1, 32, fp);
   count += fwrite(&Peerip,      1,  4, fp);
   count += fwrite(&Errorlog,    1,  1, fp);
   count += fwrite(&Trace,       1,  4, fp);
   count += fwrite(&Mfee,        1,  8, fp);
   count += fwrite(&Difficulty,  1,  4, fp);
   count += fwrite(&Time0,       1,  4, fp);
   if(count != (8+32+32+4+1+4+8+4+4)) {
      fclose(fp);
      goto bad;
   }
   fclose(fp);
   return VEOK;
}  /* write_global() */


void crctx(TX *tx)
{
   put16(CRC_VAL_PTR(tx), crc16(CRC_BUFF(tx), CRC_COUNT));
}


/* Compute mining reward and copy to reward
 * It is a function of block number:
 *
 * Starting Reward: 12A05F200
 * Per Block Increment/Decrement: 0xDAC0
 * Premine: 49E57D6596BC00
 * Mining Distribution: 0x010000bf6a05f200  (blocks 1-0x20001)
 *
 *  if(--blocknum < 0) reward = 0;
 *  else if (blocknum < 1048576)
 *     reward = 5000000000 + (56000 * blocknum);
 *  else if (blocknum <= 2097152)
 *     reward = 5000000000 + (56000 * (2097152 - blocknum));
 *  else
 *     reward = 0;
 */
void get_mreward(word32 *reward, word32 *bnum)
{
   int result;
   byte bnum2[8];
   static word32 delta[2] = { 0xDAC0, 0 };      /* reward delta 56000 */
   static word32 base1[2] = { 0x2A05F200, 1 };  /* base 5000000000 */
   static word32 t1[2] =  { 0x100000, 0 };      /* block 1M */
   static word32 t2[2] =  { 0x200000, 0 };      /* block 2M */

   if(sub64(bnum, One, bnum2)) goto noreward;
   result = cmp64(bnum2, t1);
   if(result < 0) {
      /* bnum < inflection point, t1 */
      mult64(delta, bnum2, reward);
      add64(reward, base1, reward);
      goto out;
   }
   if(sub64(t2, bnum2, reward)) {
noreward:
      reward[0] = reward[1] = 0;  /* at t2, reward is zero */
      goto out;
   }
   mult64(delta, reward, reward);
   add64(reward, base1, reward);
out:
   if(Trace) 
      plog("reward: 0x%s", bnum2hex((byte *) reward));
}  /* end get_mreward() */


/* Get exclusive lock on lockfile.
 * Returns: -1 if lock not made within 'seconds'
 *          else a descriptor to be used with unlock()
 */
int lock(char *lockfile, int seconds)
{
   time_t timeout;
   int fd, status;

   timeout = time(NULL) + seconds;
   fd = open(lockfile, O_NONBLOCK | O_RDONLY);
   if(fd == -1) fatal("lock(): missing lock file");
   for(;;) {
      status = flock(fd, LOCK_EX | LOCK_NB);
      if(status == 0) return fd;
      if(time(NULL) >= timeout) {
         close(fd);
         return -1;
      }
   }
}


/* Unlock a decriptor returned from lock() */
int unlock(int fd)
{
   int status;

   status =  flock(fd, LOCK_UN);
   close(fd);
   return status;
}


int append_tfile(char *fname, char *tfile)
{
   BTRAILER bt;
   FILE *fp;

   if(readtrailer(&bt, fname) != VEOK) goto err;
   fp = fopen(tfile, "ab");
   if(!fp) goto err;
   if(fwrite(&bt, 1, sizeof(BTRAILER), fp) != sizeof(BTRAILER)) {
      fclose(fp);
err:
      return error("Cannot append_tfile()");
   }
   fclose(fp);
   return VEOK;
}
