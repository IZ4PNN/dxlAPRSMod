/*
 * dxlAPRS toolchain
 *
 * Copyright (C) Christian Rabler <oe5dxl@oevsv.at>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
/* "@(#)sondeaprs.c Jan 12 21:19:37 2015" */


#define X2C_int32
#define X2C_index32
#ifndef sondeaprs_H_
#include "sondeaprs.h"
#endif
#define sondeaprs_C_
#ifndef aprsstr_H_
#include "aprsstr.h"
#endif
#ifndef osi_H_
#include "osi.h"
#endif
#ifndef RealMath_H_
#include "RealMath.h"
#endif
#ifndef InOut_H_
#include "InOut.h"
#endif
#ifndef udp_H_
#include "udp.h"
#endif
#ifndef TimeConv_H_
#include "TimeConv.h"
#endif

char sondeaprs_mycall[100];
char sondeaprs_via[100];
char sondeaprs_destcall[100];
char sondeaprs_objname[100];
char sondeaprs_commentfn[1025];
char sondeaprs_sym[2];
unsigned long sondeaprs_beacontime;
unsigned long sondeaprs_toport;
unsigned long sondeaprs_ipnum;
char sondeaprs_verb;
char sondeaprs_nofilter;
long sondeaprs_comptyp;
long sondeaprs_micessid;
long sondeaprs_udpsock;
char sondeaprs_anyip;
char sondeaprs_sendmon;
char sondeaprs_dao;
/* encode demodulated sonde to aprs axudp by OE5DXL */
#define sondeaprs_CR "\015"

#define sondeaprs_LF "\012"

#define sondeaprs_KNOTS 1.851984

#define sondeaprs_FEET 3.2808398950131

#define sondeaprs_LINESBUF 4

#define sondeaprs_PI 3.1415926535898

#define sondeaprs_GPSTIMECORR 14

#define sondeaprs_DAYSEC 86400

#define sondeaprs_RAD 1.7453292519943E-2

#define sondeaprs_MAXHRMS 50.0
/* not send if gps h pos spreads more meters */

#define sondeaprs_MAXVRMS 500.0
/* not send if gps v pos spreads more meters */

enum ERRS {sondeaprs_ePRES, sondeaprs_eTEMP, sondeaprs_eHYG,
                sondeaprs_eSPEED, sondeaprs_eDIR, sondeaprs_eLAT,
                sondeaprs_eLONG, sondeaprs_eALT, sondeaprs_eMISS,
                sondeaprs_eRMS};


struct DATLINE;


struct DATLINE {
   double hpa;
   double temp;
   double hyg;
   double alt;
   double speed;
   double dir;
   double lat;
   double long0;
   double climb0;
   double clb;
   unsigned long time0;
   unsigned long uptime;
};

struct POSITION;


struct POSITION {
   double long0;
   double lat;
};

/*CRCL, CRCH: ARRAY[0..255] OF SET8;*/
static struct DATLINE dat[4];

static char s[101];

static char h[101];

static double speedsum;

static unsigned long speedcnt;

static unsigned short chk;

static unsigned long systime;

static unsigned long lastbeacon;

static unsigned long commentline;


static unsigned long truncc(double r)
{
   if (r<=0.0) return 0UL;
   else if (r>=2.147483647E+9) return 2147483647UL;
   else return (unsigned long)X2C_TRUNCC(r,0UL,X2C_max_longcard);
   return 0;
} /* end truncc() */


static unsigned long truncr(double r)
{
   if (r<=0.0) return 0UL;
   else if (r>=2.147483647E+9) return 2147483647UL;
   else return (unsigned long)X2C_TRUNCC(r,0UL,X2C_max_longcard);
   return 0;
} /* end truncr() */


extern long sondeaprs_GetIp(char h0[], unsigned long h_len,
                unsigned long * p, unsigned long * ip, unsigned long * port)
{
   unsigned long n;
   unsigned long i;
   char ok0;
   long sondeaprs_GetIp_ret;
   X2C_PCOPY((void **)&h0,h_len);
   *p = 0UL;
   h0[h_len-1] = 0;
   *ip = 0UL;
   for (i = 0UL; i<=4UL; i++) {
      n = 0UL;
      ok0 = 0;
      while ((unsigned char)h0[*p]>='0' && (unsigned char)h0[*p]<='9') {
         ok0 = 1;
         n = (n*10UL+(unsigned long)(unsigned char)h0[*p])-48UL;
         ++*p;
      }
      if (!ok0) {
         sondeaprs_GetIp_ret = -1L;
         goto label;
      }
      if (i<3UL) {
         if (h0[*p]!='.' || n>255UL) {
            sondeaprs_GetIp_ret = -1L;
            goto label;
         }
         *ip =  *ip*256UL+n;
      }
      else if (i==3UL) {
         *ip =  *ip*256UL+n;
         if (h0[*p]!=':' || n>255UL) {
            sondeaprs_GetIp_ret = -1L;
            goto label;
         }
      }
      else if (n>65535UL) {
         sondeaprs_GetIp_ret = -1L;
         goto label;
      }
      *port = n;
      ++*p;
   } /* end for */
   sondeaprs_GetIp_ret = 0L;
   label:;
   X2C_PFREE(h0);
   return sondeaprs_GetIp_ret;
} /* end GetIp() */


static void comment0(char buf[], unsigned long buf_len)
{
   long len;
   long lc;
   long eol;
   long bol;
   long i;
   long f;
   char fb[32768];
   buf[0UL] = 0;
   len = 0L;
   if (sondeaprs_commentfn[0UL]) {
      f = osi_OpenRead(sondeaprs_commentfn, 1025ul);
      if (f>=0L) {
         len = osi_RdBin(f, (char *)fb, 32768u/1u, 32767UL);
         osi_Close(f);
         while (len>0L && (unsigned char)fb[len-1L]<=' ') --len;
         if (len>0L && len<32767L) {
            fb[len] = '\012';
            ++len;
         }
         do {
            lc = (long)commentline;
            eol = 0L;
            for (;;) {
               bol = eol;
               while (eol<len && fb[eol]!='\012') ++eol;
               if (eol>=len) {
                  bol = eol;
                  if (commentline) {
                     lc = 1L;
                     commentline = 0UL;
                  }
                  break;
               }
               if (fb[bol]!='#') {
                  if (lc==0L) {
                     ++commentline;
                     break;
                  }
                  --lc;
               }
               ++eol;
            }
         } while (lc);
         i = 0L;
         while (bol<eol && i<(long)(buf_len-1)) {
            buf[i] = fb[bol];
            ++i;
            ++bol;
         }
         buf[i] = 0;
      }
      else if (sondeaprs_verb) osi_WrStrLn("beacon file not found", 22ul);
   }
} /* end comment() */


static void sendudp(char buf[], unsigned long buf_len, long len)
{
   long i;
   /*  crc:CARDINAL;  */
   X2C_PCOPY((void **)&buf,buf_len);
   /*
     IF withcrc THEN
       crc:=UDPCRC(buf, len);
       buf[len]:=CHR(crc MOD 256);
       buf[len+1]:=CHR(crc DIV 256);
       INC(len, 2);
     END;
     AppCRC(buf, len);
   */
   i = udpsend(sondeaprs_udpsock, buf, len, sondeaprs_toport,
                sondeaprs_ipnum);
   X2C_PFREE(buf);
/*
FOR i:=0 TO upos-2 DO IO.WrHex(ORD(buf[i]), 3) END; IO.WrLn;
*/
} /* end sendudp() */


static char num(unsigned long n)
{
   return (char)(n%10UL+48UL);
} /* end num() */


static unsigned long dao91(double x)
/* radix91(xx/1.1) of dddmm.mmxx */
{
   double a;
   a = fabs(x);
   return ((truncc((a-(double)(float)truncc(a))*6.E+5)%100UL)*20UL+11UL)
                /22UL;
} /* end dao91() */


static void sendaprs(unsigned long comp0, unsigned long micessid, char dao,
                unsigned long time0, char mycall[], unsigned long mycall_len,
                 char destcall[], unsigned long destcall_len, char via[],
                unsigned long via_len, char sym[], unsigned long sym_len,
                char obj[], unsigned long obj_len, double lat, double long0,
                double alt, double course, double speed, char comm[],
                unsigned long comm_len)
{
   char ds[201];
   char h0[201];
   char b[201];
   char raw[361];
   long rp;
   unsigned long micdest;
   unsigned long nl;
   unsigned long n;
   unsigned long i;
   double a;
   char tmp;
   X2C_PCOPY((void **)&mycall,mycall_len);
   X2C_PCOPY((void **)&destcall,destcall_len);
   X2C_PCOPY((void **)&via,via_len);
   X2C_PCOPY((void **)&sym,sym_len);
   X2C_PCOPY((void **)&obj,obj_len);
   X2C_PCOPY((void **)&comm,comm_len);
   /* OE0AAA-9>APERXQ,RELAY,WIDE2-2:!4805.44N/01333.64E>325/016/A=001824 */
   b[0] = 0;
   aprsstr_Append(b, 201ul, mycall, mycall_len);
   micdest = aprsstr_Length(b, 201ul)+1UL;
   aprsstr_Append(b, 201ul, ">", 2ul);
   aprsstr_Append(b, 201ul, destcall, destcall_len);
   if (micessid>0UL) {
      aprsstr_Append(b, 201ul, "-", 2ul);
      aprsstr_Append(b, 201ul, (char *)(tmp = (char)(micessid+48UL),&tmp),
                1u/1u);
   }
   if (via[0UL]) {
      aprsstr_Append(b, 201ul, ",", 2ul);
      aprsstr_Append(b, 201ul, via, via_len);
   }
   if (comp0==0UL) {
      /* uncompressed */
      aprsstr_Append(b, 201ul, ":;", 3ul);
      aprsstr_Assign(h0, 201ul, obj, obj_len);
      aprsstr_Append(h0, 201ul, "         ", 10ul);
      h0[9U] = 0;
      aprsstr_Append(b, 201ul, h0, 201ul);
      aprsstr_Append(b, 201ul, "*", 2ul);
      aprsstr_DateToStr(time0, ds, 201ul);
      ds[0U] = ds[11U];
      ds[1U] = ds[12U];
      ds[2U] = ds[14U];
      ds[3U] = ds[15U];
      ds[4U] = ds[17U];
      ds[5U] = ds[18U];
      ds[6U] = 0;
      aprsstr_Append(b, 201ul, ds, 201ul);
      aprsstr_Append(b, 201ul, "h", 2ul);
      i = aprsstr_Length(b, 201ul);
      a = fabs(lat);
      n = truncr(a);
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
      n = truncr((a-(double)(float)n)*6000.0);
      b[i] = num(n/1000UL);
      ++i;
      b[i] = num(n/100UL);
      ++i;
      b[i] = '.';
      ++i;
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
      if (lat>=0.0) b[i] = 'N';
      else b[i] = 'S';
      ++i;
      b[i] = sym[0UL];
      ++i;
      a = fabs(long0);
      n = truncr(a);
      b[i] = num(n/100UL);
      ++i;
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
      n = truncr((a-(double)(float)n)*6000.0);
      b[i] = num(n/1000UL);
      ++i;
      b[i] = num(n/100UL);
      ++i;
      b[i] = '.';
      ++i;
      b[i] = num(n/10UL);
      ++i;
      b[i] = num(n);
      ++i;
      if (lat>=0.0) b[i] = 'E';
      else b[i] = 'W';
      ++i;
      b[i] = sym[1UL];
      ++i;
      if (speed>0.5) {
         n = truncr(course+1.5);
         b[i] = num(n/100UL);
         ++i;
         b[i] = num(n/10UL);
         ++i;
         b[i] = num(n);
         ++i;
         b[i] = '/';
         ++i;
         n = truncr(speed*5.3996146834962E-1+0.5);
         b[i] = num(n/100UL);
         ++i;
         b[i] = num(n/10UL);
         ++i;
         b[i] = num(n);
         ++i;
      }
      if (alt>0.5) {
         b[i] = '/';
         ++i;
         b[i] = 'A';
         ++i;
         b[i] = '=';
         ++i;
         n = truncr(fabs(alt*3.2808398950131+0.5));
         if (alt>=0.0) b[i] = num(n/100000UL);
         else b[i] = '-';
         ++i;
         b[i] = num(n/10000UL);
         ++i;
         b[i] = num(n/1000UL);
         ++i;
         b[i] = num(n/100UL);
         ++i;
         b[i] = num(n/10UL);
         ++i;
         b[i] = num(n);
         ++i;
      }
   }
   else if (comp0==1UL) {
      /* compressed */
      aprsstr_Append(b, 201ul, ":!", 3ul);
      i = aprsstr_Length(b, 201ul);
      b[i] = sym[0UL];
      ++i;
      if (lat<90.0) n = truncr((90.0-lat)*3.80926E+5);
      else n = 0UL;
      b[i] = (char)(33UL+n/753571UL);
      ++i;
      b[i] = (char)(33UL+(n/8281UL)%91UL);
      ++i;
      b[i] = (char)(33UL+(n/91UL)%91UL);
      ++i;
      b[i] = (char)(33UL+n%91UL);
      ++i;
      if (long0>(-180.0)) n = truncr((180.0+long0)*1.90463E+5);
      else n = 0UL;
      b[i] = (char)(33UL+n/753571UL);
      ++i;
      b[i] = (char)(33UL+(n/8281UL)%91UL);
      ++i;
      b[i] = (char)(33UL+(n/91UL)%91UL);
      ++i;
      b[i] = (char)(33UL+n%91UL);
      ++i;
      b[i] = sym[1UL];
      ++i;
      if (speed>0.5) {
         b[i] = (char)(33UL+truncr(course)/4UL);
         ++i;
         b[i] = (char)(33UL+truncr((double)(RealMath_ln((float)
                (speed*5.3996146834962E-1+1.0))*1.29935872129E+1f)));
         ++i;
         b[i] = '_';
         ++i;
      }
      else if (alt>0.5) {
         if (alt*3.2808398950131>1.0) {
            n = truncr((double)(RealMath_ln((float)(alt*3.2808398950131))
                *500.5f));
         }
         else n = 0UL;
         if (n>=8281UL) n = 8280UL;
         b[i] = (char)(33UL+n/91UL);
         ++i;
         b[i] = (char)(33UL+n%91UL);
         ++i;
         b[i] = 'W';
         ++i;
      }
      else {
         b[i] = ' ';
         ++i;
         b[i] = ' ';
         ++i;
         b[i] = '_';
         ++i;
      }
      if (speed>0.5) {
         b[i] = '/';
         ++i;
         b[i] = 'A';
         ++i;
         b[i] = '=';
         ++i;
         n = truncr(alt*3.2808398950131+0.5);
         if (alt>=0.0) b[i] = num(n/10000UL);
         else b[i] = '-';
         ++i;
         b[i] = num(n/10000UL);
         ++i;
         b[i] = num(n/1000UL);
         ++i;
         b[i] = num(n/100UL);
         ++i;
         b[i] = num(n/10UL);
         ++i;
         b[i] = num(n);
         ++i;
      }
   }
   else if (comp0==2UL) {
      /* mic-e */
      aprsstr_Append(b, 201ul, ":`", 3ul);
      i = micdest;
      nl = truncr(fabs(long0));
      n = truncr(fabs(lat));
      b[i] = (char)(80UL+n/10UL);
      ++i;
      b[i] = (char)(80UL+n%10UL);
      ++i;
      n = truncr((fabs(lat)-(double)(float)n)*6000.0);
      b[i] = (char)(80UL+n/1000UL);
      ++i;
      b[i] = (char)(48UL+32UL*(unsigned long)(lat>=0.0)+(n/100UL)%10UL);
      ++i;
      b[i] = (char)(48UL+32UL*(unsigned long)(nl<10UL || nl>=100UL)+(n/10UL)
                %10UL);
      ++i;
      b[i] = (char)(48UL+32UL*(unsigned long)(long0<0.0)+n%10UL);
      i = aprsstr_Length(b, 201ul);
      if (nl<10UL) b[i] = (char)(nl+118UL);
      else if (nl>=100UL) {
         if (nl<110UL) b[i] = (char)(nl+8UL);
         else b[i] = (char)(nl-72UL);
      }
      else b[i] = (char)(nl+28UL);
      ++i;
      nl = truncr((fabs(long0)-(double)(float)nl)*6000.0); /* long min*100 */
      n = nl/100UL;
      if (n<10UL) n += 60UL;
      b[i] = (char)(n+28UL);
      ++i;
      b[i] = (char)(nl%100UL+28UL);
      ++i;
      n = truncr(speed*5.3996146834962E-1+0.5);
      b[i] = (char)(n/10UL+28UL);
      ++i;
      nl = truncr(course);
      b[i] = (char)(32UL+(n%10UL)*10UL+nl/100UL);
      ++i;
      b[i] = (char)(28UL+nl%100UL);
      ++i;
      b[i] = sym[1UL];
      ++i;
      b[i] = sym[0UL];
      ++i;
      if (alt>0.5) {
         if (alt>(-1.E+4)) n = truncr(alt+10000.5);
         else n = 0UL;
         b[i] = (char)(33UL+(n/8281UL)%91UL);
         ++i;
         b[i] = (char)(33UL+(n/91UL)%91UL);
         ++i;
         b[i] = (char)(33UL+n%91UL);
         ++i;
         b[i] = '}';
         ++i;
      }
   }
   if (dao) {
      b[i] = '!';
      ++i;
      b[i] = 'w';
      ++i;
      b[i] = (char)(33UL+dao91(lat));
      ++i;
      b[i] = (char)(33UL+dao91(long0));
      ++i;
      b[i] = '!';
      ++i;
   }
   b[i] = 0;
   aprsstr_Append(b, 201ul, comm, comm_len);
   comment0(h0, 201ul);
   aprsstr_Append(b, 201ul, h0, 201ul);
   /*  Append(b, CR+LF); */
   if (aprsstr_Length(mycall, mycall_len)>=3UL) {
      if (!sondeaprs_sendmon) {
         aprsstr_mon2raw(b, 201ul, raw, 361ul, &rp);
         if (rp>0L) sendudp(raw, 361ul, rp);
      }
      else sendudp(b, 201ul, (long)(aprsstr_Length(b, 201ul)+1UL));
   }
   if (sondeaprs_verb) osi_WrStrLn(b, 201ul);
   X2C_PFREE(mycall);
   X2C_PFREE(destcall);
   X2C_PFREE(via);
   X2C_PFREE(sym);
   X2C_PFREE(obj);
   X2C_PFREE(comm);
} /* end sendaprs() */

#define sondeaprs_Z 48


static void degtostr(double d, char lat, char form, char s0[],
                unsigned long s_len)
{
   unsigned long i;
   unsigned long n;
   if (s_len-1<11UL) {
      s0[0UL] = 0;
      return;
   }
   if (form=='2') i = 7UL;
   else if (form=='3') i = 8UL;
   else i = 9UL;
   if (d<0.0) {
      d = -d;
      if (lat) s0[i] = 'S';
      else s0[i+1UL] = 'W';
   }
   else if (lat) s0[i] = 'N';
   else s0[i+1UL] = 'E';
   if (form=='2') {
      /* DDMM.MMNDDMM.MME */
      n = truncr(d*3.4377467707849E+5+0.5);
      s0[0UL] = (char)((n/600000UL)%10UL+48UL);
      i = (unsigned long)!lat;
      s0[i] = (char)((n/60000UL)%10UL+48UL);
      ++i;
      s0[i] = (char)((n/6000UL)%10UL+48UL);
      ++i;
      s0[i] = (char)((n/1000UL)%6UL+48UL);
      ++i;
      s0[i] = (char)((n/100UL)%10UL+48UL);
      ++i;
      s0[i] = '.';
      ++i;
      s0[i] = (char)((n/10UL)%10UL+48UL);
      ++i;
      s0[i] = (char)(n%10UL+48UL);
      ++i;
   }
   else if (form=='3') {
      /* DDMM.MMMNDDMM.MMME */
      n = truncr(d*3.4377467707849E+6+0.5);
      s0[0UL] = (char)((n/6000000UL)%10UL+48UL);
      i = (unsigned long)!lat;
      s0[i] = (char)((n/600000UL)%10UL+48UL);
      ++i;
      s0[i] = (char)((n/60000UL)%10UL+48UL);
      ++i;
      s0[i] = (char)((n/10000UL)%6UL+48UL);
      ++i;
      s0[i] = (char)((n/1000UL)%10UL+48UL);
      ++i;
      s0[i] = '.';
      ++i;
      s0[i] = (char)((n/100UL)%10UL+48UL);
      ++i;
      s0[i] = (char)((n/10UL)%10UL+48UL);
      ++i;
      s0[i] = (char)(n%10UL+48UL);
      ++i;
   }
   else {
      /* DDMMSS */
      n = truncr(d*2.062648062471E+5+0.5);
      s0[0UL] = (char)((n/360000UL)%10UL+48UL);
      i = (unsigned long)!lat;
      s0[i] = (char)((n/36000UL)%10UL+48UL);
      ++i;
      s0[i] = (char)((n/3600UL)%10UL+48UL);
      ++i;
      s0[i] = 'o';
      ++i;
      s0[i] = (char)((n/600UL)%6UL+48UL);
      ++i;
      s0[i] = (char)((n/60UL)%10UL+48UL);
      ++i;
      s0[i] = '\'';
      ++i;
      s0[i] = (char)((n/10UL)%6UL+48UL);
      ++i;
      s0[i] = (char)(n%10UL+48UL);
      ++i;
      s0[i] = '\"';
      ++i;
   }
   ++i;
   s0[i] = 0;
} /* end degtostr() */


static void postostr(struct POSITION pos, char form, char s0[],
                unsigned long s_len)
{
   char h0[32];
   degtostr(pos.lat, 1, form, s0, s_len);
   aprsstr_Append(s0, s_len, "/", 2ul);
   degtostr(pos.long0, 0, form, h0, 32ul);
   aprsstr_Append(s0, s_len, h0, 32ul);
} /* end postostr() */

#define sondeaprs_RAD0 1.7453292519943E-2


static void WrDeg(double la, double lo)
{
   char s0[31];
   struct POSITION pos;
   pos.lat = la*1.7453292519943E-2;
   pos.long0 = lo*1.7453292519943E-2;
   postostr(pos, '2', s0, 31ul);
   InOut_WriteString(s0, 31ul);
} /* end WrDeg() */


static void show(struct DATLINE d)
{
   char s0[31];
   osi_WrFixed((float)d.hpa, 1L, 12UL);
   InOut_WriteString("hPa ", 5ul);
   osi_WrFixed((float)d.temp, 1L, 5UL);
   InOut_WriteString("C ", 3ul);
   InOut_WriteInt((long)truncr(d.hyg), 2UL);
   InOut_WriteString("% ", 3ul);
   InOut_WriteInt((long)X2C_TRUNCI(d.speed*3.6,X2C_min_longint,
                X2C_max_longint), 3UL);
   InOut_WriteString("km/h ", 6ul);
   InOut_WriteInt((long)truncr(d.dir), 3UL);
   InOut_WriteString("dir ", 5ul);
   WrDeg(d.lat, d.long0);
   InOut_WriteString(" ", 2ul);
   /*WrFixed(d.gpsalt, 1, 8); WrStr("m "); */
   InOut_WriteInt((long)X2C_TRUNCI(d.alt,X2C_min_longint,X2C_max_longint),
                1UL);
   InOut_WriteString("m ", 3ul);
   osi_WrFixed((float)d.climb0, 1L, 5UL);
   InOut_WriteString("m/s ", 5ul);
   osi_WrFixed((float)d.clb, 1L, 5UL);
   InOut_WriteString("m/s ", 5ul);
   aprsstr_TimeToStr(d.time0, s0, 31ul);
   InOut_WriteString(s0, 31ul);
} /* end show() */


static char Checkval(double a[], unsigned long a_len, double err,
                double min0, double max0)
{
   unsigned long i;
   double y;
   double m;
   double k;
   unsigned long tmp;
   char Checkval_ret;
   X2C_PCOPY((void **)&a,a_len*sizeof(double));
   tmp = a_len-1;
   i = 0UL;
   if (i<=tmp) for (;; i++) {
      if (a[i]<min0 || a[i]>max0) {
         Checkval_ret = 0;
         goto label;
      }
      if (i==tmp) break;
   } /* end for */
   k = 0.0;
   tmp = (a_len-1)-1UL;
   i = 0UL;
   if (i<=tmp) for (;; i++) {
      k = (k+a[i+1UL])-a[i]; /* median slope */
      if (i==tmp) break;
   } /* end for */
   k = X2C_DIVL(k,(double)(float)(a_len-1));
   m = 0.0;
   tmp = (a_len-1)-1UL;
   i = 0UL;
   if (i<=tmp) for (;; i++) {
      y = fabs((a[i+1UL]-a[i])-k);
      if (y>m) m = y;
      if (i==tmp) break;
   } /* end for */
   k = fabs(k);
   if (k<err) k = err;
   Checkval_ret = m<fabs(k);
   label:;
   X2C_PFREE(a);
   return Checkval_ret;
} /* end Checkval() */


static void climb(void)
{
   unsigned long i;
   double k;
   k = 0.0;
   for (i = 0UL; i<=2UL; i++) {
      k = (k+dat[i+1UL].alt)-dat[i].alt; /* median slope */
   } /* end for */
   dat[0U].climb0 = -(X2C_DIVL(k,3.0));
} /* end climb() */


static void Checkvals(unsigned short * e)
{
   unsigned long ta;
   unsigned long i;
   double v[4];
   *e = 0U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].hpa;
   } /* end for */
   if (!Checkval(v, 4ul, 2000.0, 1.0, 1100.0)) *e |= 0x1U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].temp;
   } /* end for */
   if (!Checkval(v, 4ul, 200.0, (-150.0), 80.0)) *e |= 0x2U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].hyg;
   } /* end for */
   if (!Checkval(v, 4ul, 100.0, 0.0, 100.0)) *e |= 0x4U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].speed;
   } /* end for */
   if (!Checkval(v, 4ul, 200.0, 0.0, 200.0)) *e |= 0x8U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].dir;
   } /* end for */
   if (!Checkval(v, 4ul, 360.0, 0.0, 359.0)) *e |= 0x10U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].lat;
   } /* end for */
   if (!Checkval(v, 4ul, 4.5454545454545E-4, (-85.0), 85.0)) *e |= 0x20U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].long0;
   } /* end for */
   if (!Checkval(v, 4ul, 4.5454545454545E-4, (-180.0), 180.0)) *e |= 0x40U;
   for (i = 0UL; i<=3UL; i++) {
      v[i] = dat[i].alt;
   } /* end for */
   if (!Checkval(v, 4ul, 100.0, 5.0, 60000.0)) *e |= 0x80U;
   for (i = 0UL; i<=3UL; i++) {
      if (i>0UL && ta!=(dat[i].time0+1UL)%86400UL) *e |= 0x100U;
      ta = dat[i].time0;
   } /* end for */
} /* end Checkvals() */


static void shift(void)
{
   unsigned long i;
   for (i = 3UL; i>=1UL; i--) {
      dat[i] = dat[i-1UL];
   } /* end for */
} /* end shift() */


extern void sondeaprs_senddata(double lat, double long0, double alt,
                double speed, double dir, double clb, double hp, double hyg,
                double temp, double mhz, double hrms, double vrms,
                unsigned long sattime, unsigned long uptime, char objname[],
                unsigned long objname_len)
{
   unsigned char e;
   X2C_PCOPY((void **)&objname,objname_len);
   if (aprsstr_Length(sondeaprs_mycall, 100ul)<3UL) {
      osi_WrStrLn("no tx witout <mycall>", 22ul);
      goto label;
   }
   if (aprsstr_Length(objname, objname_len)<3UL) {
      osi_WrStrLn("no tx witout <objectname>", 26ul);
      goto label;
   }
   shift();
   speedsum = speedsum+speed;
   ++speedcnt;
   dat[0U].hpa = hp;
   dat[0U].temp = temp;
   dat[0U].hyg = hyg;
   dat[0U].alt = alt;
   dat[0U].speed = X2C_DIVL(speedsum,(double)speedcnt);
   dat[0U].dir = dir;
   dat[0U].lat = X2C_DIVL(lat,1.7453292519943E-2);
   dat[0U].long0 = X2C_DIVL(long0,1.7453292519943E-2);
   dat[0U].time0 = ((sattime+86400UL)-14UL)%86400UL;
   dat[0U].uptime = (((sattime+86400UL)-14UL)-uptime)%86400UL;
   dat[0U].clb = clb;
   climb();
   Checkvals(&chk);
   if (hrms>50.0 || vrms>500.0) chk |= 0x200U;
   if (sondeaprs_verb) {
      osi_WrStrLn("", 1ul);
      show(dat[0U]);
      InOut_WriteString(" ", 2ul);
      for (e = sondeaprs_ePRES;; e++) {
         if (X2C_IN((long)e,10,chk)) {
            switch ((unsigned)e) {
            case sondeaprs_ePRES:
               InOut_WriteString("p", 2ul);
               break;
            case sondeaprs_eTEMP:
               InOut_WriteString("t", 2ul);
               break;
            case sondeaprs_eHYG:
               InOut_WriteString("h", 2ul);
               break;
            case sondeaprs_eSPEED:
               InOut_WriteString("v", 2ul);
               break;
            case sondeaprs_eDIR:
               InOut_WriteString("d", 2ul);
               break;
            case sondeaprs_eLAT:
               InOut_WriteString("y", 2ul);
               break;
            case sondeaprs_eLONG:
               InOut_WriteString("x", 2ul);
               break;
            case sondeaprs_eALT:
               InOut_WriteString("a", 2ul);
               break;
            case sondeaprs_eMISS:
               InOut_WriteString("s", 2ul);
               break;
            case sondeaprs_eRMS: /*WrFixed(vrms, 1,5); WrStr(" ");
                WrFixed(hrms, 1,5);*/
               InOut_WriteString("r", 2ul);
               break;
            default:
               X2C_TRAP(X2C_CASE_TRAP);
            } /* end switch */
         }
         if (e==sondeaprs_eRMS) break;
      } /* end for */
   }
   systime = TimeConv_time();
   if (lastbeacon+sondeaprs_beacontime<=systime && (sondeaprs_nofilter || (chk&0x3E0U)
                ==0U)) {
      strncpy(s,"Clb=",101u);
      aprsstr_FixToStr((float)clb, 2UL, h, 101ul); /*dat[0].climb*/
      aprsstr_Append(s, 101ul, h, 101ul);
      aprsstr_Append(s, 101ul, "m/s", 4ul);
      if ((0x1U & chk)==0) {
         aprsstr_Append(s, 101ul, " p=", 4ul);
         aprsstr_FixToStr((float)dat[0U].hpa, 2UL, h, 101ul);
         aprsstr_Append(s, 101ul, h, 101ul);
         aprsstr_Append(s, 101ul, "hPa", 4ul);
      }
      if ((0x2U & chk)==0) {
         aprsstr_Append(s, 101ul, " t=", 4ul);
         aprsstr_FixToStr((float)dat[0U].temp, 2UL, h, 101ul);
         aprsstr_Append(s, 101ul, h, 101ul);
         aprsstr_Append(s, 101ul, "C", 2ul);
      }
      if (hyg>=0.5 && (0x4U & chk)==0) {
         aprsstr_Append(s, 101ul, " h=", 4ul);
         aprsstr_IntToStr((long)truncc(dat[0U].hyg+0.5), 1UL, h, 101ul);
         aprsstr_Append(s, 101ul, h, 101ul);
         aprsstr_Append(s, 101ul, "%", 2ul);
      }
      aprsstr_Append(s, 101ul, " ", 2ul);
      aprsstr_FixToStr((float)mhz, 3UL, h, 101ul);
      aprsstr_Append(s, 101ul, h, 101ul);
      aprsstr_Append(s, 101ul, "MHz", 4ul);
      if (uptime%300UL==0UL) {
         aprsstr_Append(s, 101ul, " powerup=", 10ul);
         aprsstr_TimeToStr(dat[0U].uptime, h, 101ul);
         aprsstr_Append(s, 101ul, h, 101ul);
      }
      if (aprsstr_Length(sondeaprs_mycall, 100ul)>=3UL) {
         sendaprs(0UL, 0UL, sondeaprs_dao, dat[0U].time0, sondeaprs_mycall,
                100ul, sondeaprs_destcall, 100ul, sondeaprs_via, 100ul,
                sondeaprs_sym, 2ul, objname, objname_len, dat[0U].lat,
                dat[0U].long0, dat[0U].alt,
                (double)(float)(truncc(dat[0U].dir)%360UL),
                dat[0U].speed*3.6, s, 101ul);
         lastbeacon = systime;
      }
      speedcnt = 0UL;
      speedsum = 0.0;
   }
   label:;
   X2C_PFREE(objname);
} /* end senddata() */


extern void sondeaprs_BEGIN(void)
{
   static int sondeaprs_init = 0;
   if (sondeaprs_init) return;
   sondeaprs_init = 1;
   RealMath_BEGIN();
   TimeConv_BEGIN();
   osi_BEGIN();
   aprsstr_BEGIN();
   sondeaprs_udpsock = -1L;
   sondeaprs_mycall[0UL] = 0;
   sondeaprs_commentfn[0UL] = 0;
   strncpy(sondeaprs_destcall,"APLWS2",100u);
   sondeaprs_via[0UL] = 0;
   strncpy(sondeaprs_sym,"/O",2u);
   sondeaprs_objname[0UL] = 0;
   sondeaprs_beacontime = 30UL;
   sondeaprs_nofilter = 0;
   memset((char *)dat,(char)0,sizeof(struct DATLINE [4]));
   lastbeacon = 0UL;
   commentline = 0UL;
   speedcnt = 0UL;
   speedsum = 0.0;
}
