/* m_stats.c
 * Copyright (c) 2004, The Bahamut Development Team and Aaron Wiebe
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
 *
 *   This program is free softwmare; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <sys/stat.h>
#include <utmp.h>
#include <fcntl.h>
#include "h.h"
#include "zlink.h"
#include "userban.h"
#include "blalloc.h"
#include "throttle.h"
#include "whowas.h"
#include "res.h"

extern float curSendK, curRecvK;
extern aWhowas WHOWAS[];
extern aCache *cachetop;
#ifdef DEBUGMODE
extern void report_fds(aClient *);
#endif

/* internal function defines */

static void show_opers(aClient *, char *);
static void show_servers(aClient *, char *);
static void count_memory(aClient *, char *);
#ifdef DEBUGMODE
static void send_usage(aClient *, char *);
#endif
static void count_whowas_memory(int *, u_long *);
static void serv_info(aClient *, char *);
static void tstats(aClient *, char *);
static u_long cres_mem(aClient *);

/* support functions */
/* show_opers
 * replies to stats p requests
 */

static u_long 
cres_mem(aClient *sptr)
{
    aCache *c = cachetop;
    struct hostent *h;
    int i;
    u_long      nm = 0, im = 0, sm = 0, ts = 0;

    for (; c; c = c->list_next)
    {
    sm += sizeof(*c);
    h = &c->he;
    for (i = 0; h->h_addr_list[i]; i++)
    {
        im += sizeof(char *);
        im += sizeof(struct in_addr);
    }
    im += sizeof(char *);

    for (i = 0; h->h_aliases[i]; i++)
    {
        nm += sizeof(char *);

        nm += strlen(h->h_aliases[i]);
    }
    nm += i - 1;
    nm += sizeof(char *);

    if (h->h_name)
        nm += strlen(h->h_name);
    }
    ts = ARES_CACSIZE * sizeof(CacheTable);
    sendto_one(sptr, ":%s %d %s :RES table sz %d",
           me.name, RPL_STATSDEBUG, sptr->name, ts);
    sendto_one(sptr, ":%s %d %s :RES Structs sz %d IP storage sz %d "
           "Name storage sz %d", me.name, RPL_STATSDEBUG, sptr->name, sm,
           im, nm);
    return ts + sm + im + nm;
}

static void
count_whowas_memory(int *wwu, u_long *wwum)
{
    aWhowas *tmp;
    int i;
    int         u = 0;
    u_long      um = 0;

    /* count the number of used whowas structs in 'u' */
    /* count up the memory used of whowas structs in um */

    for (i = 0, tmp = &WHOWAS[0]; i < NICKNAMEHISTORYLENGTH; i++, tmp++)
    if (tmp->hashv != -1)
    {
        u++;
        um += sizeof(aWhowas);
    }
    *wwu = u;
    *wwum = um;
    return;
}


#ifdef DEBUGMODE
static void
send_usage(aClient *cptr, char *nick)
{

#if defined( HAVE_GETRUSAGE )
    struct rusage rus;
    time_t      secs, rup;

#ifdef  hz
#define hzz hz
#else
#ifdef HZ
#define hzz HZ
#else
    int         hzz = 1;

#endif
#endif

    if (getrusage(RUSAGE_SELF, &rus) == -1) {
        sendto_one(cptr, ":%s NOTICE %s :Getruseage error: %s.",
                   me.name, nick, sys_errlist[errno]);
        return;
    }
    secs = rus.ru_utime.tv_sec + rus.ru_stime.tv_sec;
    rup = timeofday - me.since;
    if (secs == 0)
        secs = 1;

    sendto_one(cptr,
               ":%s %d %s :CPU Secs %d:%d User %d:%d System %d:%d",
               me.name, RPL_STATSDEBUG, nick, secs / 60, secs % 60,
               rus.ru_utime.tv_sec / 60, rus.ru_utime.tv_sec % 60,
               rus.ru_stime.tv_sec / 60, rus.ru_stime.tv_sec % 60);
    sendto_one(cptr, ":%s %d %s :RSS %d ShMem %d Data %d Stack %d",
               me.name, RPL_STATSDEBUG, nick, rus.ru_maxrss,
               rus.ru_ixrss / (rup * hzz), rus.ru_idrss / (rup * hzz),
               rus.ru_isrss / (rup * hzz));
    sendto_one(cptr, ":%s %d %s :Swaps %d Reclaims %d Faults %d",
               me.name, RPL_STATSDEBUG, nick, rus.ru_nswap,
               rus.ru_minflt, rus.ru_majflt);
    sendto_one(cptr, ":%s %d %s :Block in %d out %d",
               me.name, RPL_STATSDEBUG, nick, rus.ru_inblock,
               rus.ru_oublock);
    sendto_one(cptr, ":%s %d %s :Msg Rcv %d Send %d",
               me.name, RPL_STATSDEBUG, nick, rus.ru_msgrcv, rus.ru_msgsnd);
    sendto_one(cptr, ":%s %d %s :Signals %d Context Vol. %d Invol %d",
               me.name, RPL_STATSDEBUG, nick, rus.ru_nsignals,
               rus.ru_nvcsw, rus.ru_nivcsw);
#else
#if defined( HAVE_TIMES )
    struct tms  tmsbuf;
    time_t      secs, mins;
    int         hzz = 1, ticpermin;
    int         umin, smin, usec, ssec;

    ticpermin = hzz * 60;

    umin = tmsbuf.tms_utime / ticpermin;
    usec = (tmsbuf.tms_utime % ticpermin) / (float) hzz;
    smin = tmsbuf.tms_stime / ticpermin;
    ssec = (tmsbuf.tms_stime % ticpermin) / (float) hzz;
    secs = usec + ssec;
    mins = (secs / 60) + umin + smin;
    secs %= hzz;
    if (times(&tmsbuf) == -1) {
        sendto_one(cptr, ":%s %d %s :times(2) error: %s.",
                   me.name, RPL_STATSDEBUG, nick, strerror(errno));
        return;
    }
    secs = tmsbuf.tms_utime + tmsbuf.tms_stime;

    sendto_one(cptr,
               ":%s %d %s :CPU Secs %d:%d User %d:%d System %d:%d",
               me.name, RPL_STATSDEBUG, nick, mins, secs, umin, usec,
               smin, ssec);
#endif /* HAVE_TIMES */
#endif /* HAVE_GETRUSAGE */
    sendto_one(cptr, ":%s %d %s :Reads %d Writes %d",
               me.name, RPL_STATSDEBUG, nick, readcalls, writecalls);
    sendto_one(cptr, ":%s %d %s :DBUF alloc %d used %d",
               me.name, RPL_STATSDEBUG, nick, DBufCount, DBufUsedCount);
    sendto_one(cptr,
               ":%s %d %s :Writes:  <0 %d 0 %d <16 %d <32 %d <64 %d",
               me.name, RPL_STATSDEBUG, nick,
               writeb[0], writeb[1], writeb[2], writeb[3], writeb[4]);
    sendto_one(cptr,
               ":%s %d %s :<128 %d <256 %d <512 %d <1024 %d >1024 %d",
               me.name, RPL_STATSDEBUG, nick,
               writeb[5], writeb[6], writeb[7], writeb[8], writeb[9]);
    return;
}
#endif  /* DEBUGMODE */


static void 
count_memory(aClient *cptr, char *nick)
{
    extern aChannel *channel;
    extern aClass *classes;

    extern BlockHeap *free_local_aClients;
    extern BlockHeap *free_Links;
    extern BlockHeap *free_DLinks;
    extern BlockHeap *free_remote_aClients;
    extern BlockHeap *free_anUsers;
    extern BlockHeap *free_channels;
    extern BlockHeap *free_chanMembers;
#ifdef FLUD
    extern BlockHeap *free_fludbots;
#endif

    extern aMotd      *motd;
#ifdef SHORT_MOTD
    extern aMotd      *shortmotd;
#endif
    extern aMotd      *helpfile;

    extern int num_msg_trees;

    aClient *acptr;
    Link   *link;
    chanMember *cm;
    aBan   *bp;
    aChannel *chptr;
    aClass *cltmp;
    aMotd *amo;

    int         lc = 0;         /* local clients */
    int         lcc = 0;        /* local client conf links */
    int         rc = 0;         /* remote clients */
    int         us = 0;         /* user structs */
    int         chu = 0;        /* channel users */
    int         chi = 0;        /* channel invites */
    int         chb = 0;        /* channel bans */
    int         wwu = 0;        /* whowas users */
    int         cl = 0;         /* classes */
    int         co = 0;         /* conf lines */
    int         ch = 0;
    int         usi = 0;        /* users invited */
    int         usc = 0;        /* users in channels */
    int         usdm = 0;       /* dccallow local */
    int         usdr = 0;       /* dccallow remote */
    int         uss = 0;        /* silenced users */
    int         aw = 0;         /* aways set */
    int         number_servers_cached;  /* number of servers cached by
                                         * scache
                                         */
    u_long      chbm = 0;       /* memory used by channel bans */
    u_long      lcm = 0;        /* memory used by local clients */
    u_long      rcm = 0;        /* memory used by remote clients */
    u_long      awm = 0;        /* memory used by aways */
    u_long      wwm = 0;        /* whowas array memory used */
    u_long      com = 0;        /* memory used by conf lines */
    size_t      db = 0, db2 = 0;        /* memory used by dbufs */
    u_long      rm = 0;         /* res memory used */
    u_long      mem_servers_cached;     /* memory used by scache */

    u_long      totcl = 0;
    u_long      totch = 0;
    u_long      totww = 0;
    u_long      totmisc = 0;
    u_long      tothash = 0;
    u_long      totuban = 0;
    u_long      tot = 0;

    int wlh=0, wle=0; /* watch headers/entries */
    u_long wlhm=0; /* memory used by watch */

    int lcalloc = 0;    /* local clients allocated */
    int rcalloc = 0;    /* remote clients allocated */
    int useralloc = 0;  /* allocated users */
    int linkalloc = 0;  /* allocated links */
    int dlinkalloc = 0; /* allocated dlinks */
    int totallinks = 0; /* total links used */
    int chanalloc = 0; /* total channels alloc'd */
    int cmemballoc = 0;
    u_long lcallocsz = 0, rcallocsz = 0; /* size for stuff above */
    u_long userallocsz = 0, linkallocsz = 0, dlinkallocsz = 0, chanallocsz = 0, cmemballocsz = 0;

    int fludalloc = 0;
    u_long fludallocsz = 0;
    int fludlink = 0;

    int motdlen = 0;

    int servn = 0;

    count_whowas_memory(&wwu, &wwm);    /* no more away memory to count */

    count_watch_memory(&wlh, &wlhm);

    for(acptr = client; acptr; acptr = acptr->next)
    {
        if(MyConnect(acptr))
        {
            lc++;
            wle += acptr->watches;
        }
        else
            rc++;


#ifdef FLUD
        for (link = acptr->fludees; link;
             link = link->next)
            fludlink++;
#endif
        if (acptr->serv)
            servn++;

        if (acptr->user)
        {
            us++;
            for (link = acptr->user->invited; link;
                 link = link->next)
                usi++;
            for (link = acptr->user->channel; link;
                 link = link->next)
                usc++;
            for (link = acptr->user->dccallow; link;
                 link = link->next)
            {
                if(link->flags == DCC_LINK_ME)
                    usdm++;
                else
                    usdr++;
            }
            for (link = acptr->user->silence; link;
                 link = link->next)
                uss++;
            if (acptr->user->away)
            {
                aw++;
                awm += (strlen(acptr->user->away) + 1);
            }
        }
    }

    lcm = lc * CLIENT_LOCAL_SIZE;
    rcm = rc * CLIENT_REMOTE_SIZE;

    for (chptr = channel; chptr; chptr = chptr->nextch)
    {
        ch++;

        for (cm = chptr->members; cm; cm = cm->next)
            chu++;
        for (link = chptr->invites; link; link = link->next)
            chi++;
        for (bp = chptr->banlist; bp; bp = bp->next)
        {
            chb++;
            chbm += (strlen(bp->who) + strlen(bp->banstr) + 2 + sizeof(aBan));
    }
    }

    for (cltmp = classes; cltmp; cltmp = cltmp->next)
        cl++;

    for (amo = motd; amo; amo = amo->next)
        motdlen++;
#ifdef SHORT_MOTD
    for (amo = shortmotd; amo; amo = amo->next)
        motdlen++;
#endif
    for (amo = helpfile; amo; amo = amo->next)
        motdlen++;

    lcalloc = free_local_aClients->blocksAllocated *
        free_local_aClients->elemsPerBlock;
    lcallocsz = lcalloc * free_local_aClients->elemSize;

    rcalloc = free_remote_aClients->blocksAllocated *
        free_remote_aClients->elemsPerBlock;
    rcallocsz = rcalloc * free_remote_aClients->elemSize;

    useralloc = free_anUsers->blocksAllocated * free_anUsers->elemsPerBlock;
    userallocsz = useralloc * free_anUsers->elemSize;

    linkalloc = free_Links->blocksAllocated * free_Links->elemsPerBlock;
    linkallocsz = linkalloc * free_Links->elemSize;

    dlinkalloc = free_DLinks->blocksAllocated * free_DLinks->elemsPerBlock;
    dlinkallocsz = dlinkalloc * free_DLinks->elemSize;

    chanalloc = free_channels->blocksAllocated * free_channels->elemsPerBlock;
    chanallocsz = chanalloc * free_channels->elemSize;

    cmemballoc = free_chanMembers->blocksAllocated *
        free_chanMembers->elemsPerBlock;
    cmemballocsz = cmemballoc * free_chanMembers->elemSize;

#ifdef FLUD
    fludalloc = free_fludbots->blocksAllocated * free_fludbots->elemsPerBlock;
    fludallocsz = fludalloc * free_fludbots->elemSize;
#endif

    totallinks = lcc + usi +  uss + usc + chi + wle + fludlink + usdm + usdr;

    sendto_one(cptr, ":%s %d %s :Memory Use Summary",
               me.name, RPL_STATSDEBUG, nick);
    sendto_one(cptr, ":%s %d %s :Client usage %d(%d) ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, lc + rc, lcm + rcm,
               lcalloc + rcalloc, lcallocsz + rcallocsz);
    sendto_one(cptr, ":%s %d %s :   Local %d(%d) ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, lc, lcm, lcalloc, lcallocsz);
    sendto_one(cptr, ":%s %d %s :   Remote %d(%d) ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, rc, rcm, rcalloc, rcallocsz);
    sendto_one(cptr, ":%s %d %s :Users %d(%d) ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, us, us * sizeof(anUser),
               useralloc, userallocsz);

    totcl = lcallocsz + rcallocsz + userallocsz;

    sendto_one(cptr, ":%s %d %s :Links %d(%d) ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, totallinks,
               totallinks * sizeof(Link), linkalloc, linkallocsz);
    sendto_one(cptr, ":%s %d %s :   UserInvites %d(%d) ChanInvites %d(%d)",
               me.name, RPL_STATSDEBUG, nick, usi, usi * sizeof(Link), chi,
               chi * sizeof(Link));
    sendto_one(cptr, ":%s %d %s :   UserChannels %d(%d)",
               me.name, RPL_STATSDEBUG, nick, usc, usc * sizeof(Link));
    sendto_one(cptr, ":%s %d %s :   DCCAllow Local %d(%d) Remote %d(%d)",
               me.name, RPL_STATSDEBUG, nick, usdm, usdm * sizeof(Link),
               usdr, usdr * sizeof(Link));
    sendto_one(cptr, ":%s %d %s :   WATCH entries %d(%d)",
               me.name, RPL_STATSDEBUG, nick, wle, wle*sizeof(Link));
    sendto_one(cptr, ":%s %d %s :   Fludees %d(%d)",
               me.name, RPL_STATSDEBUG, nick, fludlink, fludlink*sizeof(Link));

    sendto_one(cptr, ":%s %d %s :DLinks ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, dlinkalloc, dlinkallocsz);
    /* Print summary of DLINKs used in clientlist.c */
    print_list_memory(cptr);

    sendto_one(cptr, ":%s %d %s :WATCH headers %d(%d)",
               me.name, RPL_STATSDEBUG, nick, wlh, wlhm);
    sendto_one(cptr, ":%s %d %s :Conflines %d(%d)",
               me.name, RPL_STATSDEBUG, nick, co, com);
    sendto_one(cptr, ":%s %d %s :Classes %d(%d)",
               me.name, RPL_STATSDEBUG, nick, cl, cl * sizeof(aClass));
    sendto_one(cptr, ":%s %d %s :Away Messages %d(%d)",
               me.name, RPL_STATSDEBUG, nick, aw, awm);
    sendto_one(cptr, ":%s %d %s :MOTD structs %d(%d)",
               me.name, RPL_STATSDEBUG, nick, motdlen,
               motdlen * sizeof(aMotd));
    sendto_one(cptr, ":%s %d %s :Servers %d(%d)",
               me.name, RPL_STATSDEBUG, nick, servn, servn * sizeof(aServer));
    sendto_one(cptr, ":%s %d %s :Message Trees %d(%d)",
               me.name, RPL_STATSDEBUG, nick, num_msg_trees,
               num_msg_trees * sizeof(MESSAGE_TREE));

    totmisc = wlhm + com + (cl * sizeof(aClass)) + awm +
        (motdlen * sizeof(aMotd)) + (servn * sizeof(aServer)) +
        (num_msg_trees * sizeof(MESSAGE_TREE));

    sendto_one(cptr, ":%s %d %s :Fludbots ALLOC %d(%d)",
               me.name, RPL_STATSDEBUG, nick, fludalloc, fludallocsz);

    sendto_one(cptr, ":%s %d %s :Channels %d(%d) ALLOC %d(%d) Bans %d(%d) "
               "Members %d(%d) ALLOC %d(%d)", me.name, RPL_STATSDEBUG, nick,
               ch, ch * sizeof(aChannel), chanalloc, chanallocsz, chb, chbm,
               chu, chu * sizeof(chanMember), cmemballoc, cmemballocsz);

    totch = chanallocsz + cmemballocsz + chbm;

    /* print userban summary, get userban total usage */
    totuban = count_userbans(cptr);
    totuban += count_simbans(cptr);

    sendto_one(cptr, ":%s %d %s :Whowas users %d(%d)",
               me.name, RPL_STATSDEBUG, nick, wwu, wwu * sizeof(anUser));
    sendto_one(cptr, ":%s %d %s :Whowas array %d(%d)",
               me.name, RPL_STATSDEBUG, nick, NICKNAMEHISTORYLENGTH, wwm);

    totww = wwu * sizeof(anUser) + wwm;

    sendto_one(cptr, ":%s %d %s :Hash: client %d(%d) chan %d(%d) whowas "
               "%d(%d) watch %d(%d)", me.name, RPL_STATSDEBUG, nick,
               U_MAX, sizeof(aHashEntry) * U_MAX,
               CH_MAX, sizeof(aHashEntry) * CH_MAX,
               WW_MAX, sizeof(aWhowas *) * WW_MAX,
               WATCHHASHSIZE, sizeof(aWatch *) * WATCHHASHSIZE);

    count_dbuf_memory(&db, &db2);
    sendto_one(cptr, ":%s %d %s :Dbuf blocks %d(%d) MAX %d(%d)",
               me.name, RPL_STATSDEBUG, nick, DBufUsedCount, db2,
               DBufCount, db);

    rm = cres_mem(cptr);

    count_scache(&number_servers_cached, &mem_servers_cached);

    sendto_one(cptr, ":%s %d %s :scache %d(%d)",
               me.name, RPL_STATSDEBUG, nick,
               number_servers_cached,
               mem_servers_cached);

    tothash = (sizeof(aHashEntry)*U_MAX)+(sizeof(aHashEntry)*CH_MAX) +
        (sizeof(aWatch *)*WATCHHASHSIZE) + (sizeof(aWhowas *)*WW_MAX);

    tot = totww + totch + totcl + totmisc + db + rm + tothash + linkallocsz +
          dlinkallocsz + fludallocsz + totuban;

    sendto_one(cptr, ":%s %d %s :whowas %d chan %d client/user %d misc %d "
               "dbuf %d hash %d res %d link %d flud %d simuserban %d",
               me.name, RPL_STATSDEBUG, nick, totww, totch, totcl, totmisc,
               db, tothash, rm, linkallocsz, fludallocsz, totuban);

    sendto_one(cptr, ":%s %d %s :TOTAL: %d sbrk(0)-etext: %u",
               me.name, RPL_STATSDEBUG, nick, tot,
               (u_int) sbrk((size_t) 0) - (u_int) sbrk0);
    return;
}


static void 
show_opers(aClient *cptr, char *name) 
{
    aClient *cptr2;
    DLink *lp;
    int j = 0;

    for (lp = oper_list; lp; lp = lp->next)
    {
    cptr2 = lp->value.cptr;

    if (!IsAnOper(cptr))
    {
        if (cptr2->umode & UMODE_h)
        {
        sendto_one(cptr, ":%s %d %s :%s (%s@%s) Idle: %d",
               me.name, RPL_STATSDEBUG, name, cptr2->name,
               cptr2->user->username, cptr2->user->host,
               timeofday - cptr2->user->last);
        j++;
        }
    }
    else
    {
        sendto_one(cptr, ":%s %d %s :%s (%s@%s) Idle: %d",
               me.name, RPL_STATSDEBUG, name, cptr2->name,
               cptr2->user->username, cptr2->user->host,
               timeofday - cptr2->user->last);
        j++;
    }
    }
    sendto_one(cptr, ":%s %d %s :%d OPER%s", me.name, RPL_STATSDEBUG,
           name, j, (j == 1) ? "" : "s");
}

/* show_servers
 * replies to stats v requests
 */
static void 
show_servers(aClient *cptr, char *name)
{
    aClient *cptr2;
    DLink *lp;
    int j = 0;

    for (lp = server_list; lp; lp = lp->next)
    {
    cptr2 = lp->value.cptr;

#ifdef HIDEULINEDSERVS
    if(IsULine(cptr2) && !IsAnOper(cptr))
        continue;
#endif
    j++;
    sendto_one(cptr, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
           me.name, RPL_STATSDEBUG, name, cptr2->name,
           (cptr2->serv->bynick[0] ? cptr2->serv->bynick : "Remote."),
           (cptr2->serv->byuser[0] ? cptr2->serv->byuser : "*"),
           (cptr2->serv->byhost[0] ? cptr2->serv->byhost : "*"),
           timeofday - cptr2->lasttime);
    }
    sendto_one(cptr, ":%s %d %s :%d Server%s", me.name, RPL_STATSDEBUG,
           name, j, (j == 1) ? "" : "s");
}

/* serv_info
 * replies to stats ? requests
 */

#define _1MEG   (1024.0)
#define _1GIG   (1024.0*1024.0)
#define _1TER   (1024.0*1024.0*1024.0)
#define _GMKs(x)    ((x > _1TER) ? "Terabytes" : ((x > _1GIG) ? \
                        "Gigabytes" : \
            ((x > _1MEG) ? "Megabytes" : "Kilobytes")))
#define _GMKv(x)    ( (x > _1TER) ? (float)(x/_1TER) : ((x > _1GIG) ? \
            (float)(x/_1GIG) : ((x > _1MEG) ? (float)(x/_1MEG) :\
                        (float)x)))

static void 
serv_info(aClient *cptr, char *name)
{
    static char Lformat[] = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
    long        sendK, receiveK, uptime;
    aClient    *acptr;
    DLink      *lp;
    int         i = 0;

    sendK = receiveK = 0;

    for (lp = server_list; lp; lp = lp->next)
    {
        acptr = lp->value.cptr;

#ifdef HIDEULINEDSERVS
        if (IsULine(acptr) && !IsAnOper(cptr))
            continue;
#endif
        sendK += acptr->sendK;
        receiveK += acptr->receiveK;
        sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO,
                    name, get_client_name(acptr, HIDEME),
                    (int) DBufLength(&acptr->sendQ),
                    (int) acptr->sendM, (int) acptr->sendK,
                    (int) acptr->receiveM, (int) acptr->receiveK,
                    timeofday - acptr->firsttime, timeofday - acptr->since,
                    IsServer(acptr) ? (DoesTS(acptr) ? "TS" : "NoTS") : "-");


        if(RC4EncLink(acptr))
            sendto_one(cptr, ":%s %d %s : - RC4 encrypted", me.name, 
                        RPL_STATSDEBUG, name);

        if(ZipOut(acptr))
        {
            unsigned long ib, ob;
            double rat;

            zip_out_get_stats(acptr->serv->zip_out, &ib, &ob, &rat);
            if(ib)
            {
                sendto_one(cptr, ":%s %d %s : - [O] Zip inbytes %d, "
                            "outbytes %d (%3.2f%%)", me.name, RPL_STATSDEBUG,
                             name, ib, ob, rat);
            }
        }

        if(ZipIn(acptr))
        {
            unsigned long ib, ob;
            double rat;

            zip_in_get_stats(acptr->serv->zip_in, &ib, &ob, &rat);
            if(ob)
            {
                sendto_one(cptr, ":%s %d %s : - [I] Zip inbytes %d, "
                            "outbytes %d (%3.2f%%)", me.name, RPL_STATSDEBUG,
                             name, ib, ob, rat);
            }
        }
        i++;
    }
    sendto_one(cptr, ":%s %d %s :%u total server%s",
           me.name, RPL_STATSDEBUG, name, i, (i == 1) ? "" : "s");
    sendto_one(cptr, ":%s %d %s :Sent total : %7.2f %s",
           me.name, RPL_STATSDEBUG, name, _GMKv(sendK), _GMKs(sendK));
    sendto_one(cptr, ":%s %d %s :Recv total : %7.2f %s",
           me.name, RPL_STATSDEBUG, name, _GMKv(receiveK),
           _GMKs(receiveK));

    uptime = (timeofday - me.since);
    sendto_one(cptr, ":%s %d %s :Server send: %7.2f %s (%4.1f K/s total,"
                     " %4.1f K/s current)", me.name, RPL_STATSDEBUG, name,
                     _GMKv(me.sendK), _GMKs(me.sendK), 
                    (float) ((float) me.sendK / (float) uptime), curSendK);
    sendto_one(cptr, ":%s %d %s :Server recv: %7.2f %s (%4.1f K/s total,"
                     " %4.1f K/s current)", me.name, RPL_STATSDEBUG, name, 
                    _GMKv(me.receiveK), _GMKs(me.receiveK),
                    (float) ((float) me.receiveK / (float) uptime), curRecvK);
}

/* tstats
 * responced to stats t requests (oddly enough)
 */

static void 
tstats(aClient *cptr, char *name)
{
    aClient *acptr;
    int     i;
    struct stats *sp;
    struct stats tmp;

    sp = &tmp;
    memcpy((char *) sp, (char *) ircstp, sizeof(*sp));
    for (i = 0; i < highest_fd; i++)
    {
        if (!(acptr = local[i]))
            continue;
        if (IsServer(acptr))
        {
            sp->is_sbs += acptr->sendB;
            sp->is_sbr += acptr->receiveB;
            sp->is_sks += acptr->sendK;
            sp->is_skr += acptr->receiveK;
            sp->is_sti += timeofday - acptr->firsttime;
            sp->is_sv++;
            if (sp->is_sbs > 1023)
            {
                sp->is_sks += (sp->is_sbs >> 10);
                sp->is_sbs &= 0x3ff;
            }
            if (sp->is_sbr > 1023)
            {
                sp->is_skr += (sp->is_sbr >> 10);
                sp->is_sbr &= 0x3ff;
            }

        }
        else if (IsClient(acptr))
        {
            sp->is_cbs += acptr->sendB;
            sp->is_cbr += acptr->receiveB;
            sp->is_cks += acptr->sendK;
            sp->is_ckr += acptr->receiveK;
            sp->is_cti += timeofday - acptr->firsttime;
            sp->is_cl++;
            if (sp->is_cbs > 1023)
            {
                sp->is_cks += (sp->is_cbs >> 10);
                sp->is_cbs &= 0x3ff;
            }
            if (sp->is_cbr > 1023)
            {
                sp->is_ckr += (sp->is_cbr >> 10);
                sp->is_cbr &= 0x3ff;
            }

        }
        else if (IsUnknown(acptr))
            sp->is_ni++;
    }

    sendto_one(cptr, ":%s %d %s :accepts %u refused %u",
               me.name, RPL_STATSDEBUG, name, sp->is_ac, sp->is_ref);
    sendto_one(cptr, ":%s %d %s :unknown commands %u prefixes %u",
               me.name, RPL_STATSDEBUG, name, sp->is_unco, sp->is_unpf);
    sendto_one(cptr, ":%s %d %s :nick collisions %u unknown closes %u",
               me.name, RPL_STATSDEBUG, name, sp->is_kill, sp->is_ni);
    sendto_one(cptr, ":%s %d %s :wrong direction %u empty %u",
               me.name, RPL_STATSDEBUG, name, sp->is_wrdi, sp->is_empt);
    sendto_one(cptr, ":%s %d %s :numerics seen %u mode fakes %u",
               me.name, RPL_STATSDEBUG, name, sp->is_num, sp->is_fake);
    sendto_one(cptr, ":%s %d %s :auth successes %u fails %u",
               me.name, RPL_STATSDEBUG, name, sp->is_asuc, sp->is_abad);
    sendto_one(cptr, ":%s %d %s :local connections %u udp packets %u",
               me.name, RPL_STATSDEBUG, name, sp->is_loc, sp->is_udp);
    sendto_one(cptr, ":%s %d %s :drones refused %u throttled rejections %u",
               me.name, RPL_STATSDEBUG, name, sp->is_drone, sp->is_throt);
    sendto_one(cptr, ":%s %d %s :banned users refused before ident/dns"
                     " %u after ident/dns %u", me.name, RPL_STATSDEBUG, 
                     name, sp->is_ref_1, sp->is_ref_2);
    sendto_one(cptr, ":%s %d %s :Client Server", 
                     me.name, RPL_STATSDEBUG, name);
    sendto_one(cptr, ":%s %d %s :connected %u %u",
               me.name, RPL_STATSDEBUG, name, sp->is_cl, sp->is_sv);
    sendto_one(cptr, ":%s %d %s :bytes sent %u.%uK %u.%uK",
               me.name, RPL_STATSDEBUG, name,
               sp->is_cks, sp->is_cbs, sp->is_sks, sp->is_sbs);
    sendto_one(cptr, ":%s %d %s :bytes recv %u.%uK %u.%uK",
               me.name, RPL_STATSDEBUG, name,
               sp->is_ckr, sp->is_cbr, sp->is_skr, sp->is_sbr);
    sendto_one(cptr, ":%s %d %s :time connected %u %u",
               me.name, RPL_STATSDEBUG, name, sp->is_cti, sp->is_sti);
#ifdef FLUD
    sendto_one(cptr, ":%s %d %s :CTCP Floods Blocked %u",
               me.name, RPL_STATSDEBUG, name, sp->is_flud);
#endif /* FLUD */
}


/*  m_stats and friends
 *  Carved off from s_serv.c in Feb04 by epiphani
 *  This mess of routines seemed to go better by themselves, seeing
 *  as how s_serv.c is getting massive.
 *
 * m_stats
 *      parv[0] = sender prefix
 *      parv[1] = statistics selector (defaults to Message frequency)
 *      parv[2] = server name (current server defaulted, if omitted)
 */

int m_stats(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
    static char Lformat[] = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
    static char Sformat[] = ":%s %d %s Name SendQ SendM SendBytes RcveM "
                            "RcveBytes :Open_since Idle TS";

    struct Message  *mptr;
    aClient         *acptr;
    char             stat = parc > 1 ? parv[1][0] : '\0';
    int              i, doall = 0, wilds = 0;
    char            *name;
    time_t           sincetime;
    static time_t   last_used = 0L;

#ifdef NO_USER_STATS
    if (!IsAnOper(sptr))
    {
        sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
        return 0;
    }
#else
#ifdef NO_LOCAL_USER_STATS
    if (!IsAnOper(sptr) && !MyConnect(sptr))
    {
        sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
        return 0;
    }
#endif
#endif

    if (hunt_server(cptr, sptr, ":%s STATS %s :%s", 2, parc, parv) != 
                    HUNTED_ISME)
        return 0;

    if (IsSquelch(sptr))
        return 0;

    if (!IsAnOper(sptr) && !IsULine(sptr))
    {
        /* allow remote stats p l ? u */
        if (!((stat == 'p') || (stat == 'P') || (stat=='?') || (stat=='u') ||
              (stat=='l') || (stat=='L')) && !MyConnect(sptr))
            return 0;

        /* if they're my user, penalize them. */
        if (MyConnect(sptr))
            sptr->since += 5;

        if ((last_used + MOTD_WAIT) > NOW)
            return 0;
        else
            last_used = NOW;
    }

    if (parc > 2)
    {
        name = parv[2];
        if (!mycmp(name, me.name))
            doall = 2;
        else if (match(name, me.name) == 0)
            doall = 1;
        if (strchr(name, '*') || strchr(name, '?'))
            wilds = 1;
    }
    else
        name = me.name;

    if (stat != (char) 0 && !IsULine(sptr))
        sendto_realops_lev(SPY_LEV, "STATS %c requested by %s (%s@%s) [%s]",
               stat, sptr->name, sptr->user->username,
               sptr->user->host, sptr->user->server);
    switch (stat)
    {
        case 'L':
        case 'l':
        /* changed behavior totally.  This is what we do now:
         * #1: if the user is not opered, never return ips for anything
         * #2: we DON'T deny /stats l for opers.  Ever heard of /sping?
         *     it's easy to see if you're lagging a server, why restrict
         *     something used 99% of the time for good when you're not
         *     doing any harm?
         * #3: NEVER return all users on a server, UGH, just like
         *     /trace, this was fiercely obnoxious.  If you don't
         *     add an argument, you get all SERVER links.
         */
        sendto_one(sptr, Sformat, me.name, RPL_STATSLINKINFO, parv[0]);
        if ((parc > 2) && !(doall || wilds))
        {         /* Single client lookup */
            if (!(acptr = find_person(name, NULL)))
            break;
            /*
             * sincetime might be greater than timeofday,
             * store a new value here to avoid sending
             * negative since-times. -Rak
             */
            sincetime = (acptr->since > timeofday) ? 0 : 
                                timeofday - acptr->since;
            sendto_one(sptr, Lformat, me.name, RPL_STATSLINKINFO, parv[0],
                        (IsAnOper(sptr) ? get_client_name(acptr, TRUE) :
                        get_client_name(acptr, HIDEME)),
                        (int) DBufLength(&acptr->sendQ),
                        (int) acptr->sendM, (int) acptr->sendK,
                        (int) acptr->receiveM, (int) acptr->receiveK,
                        timeofday - acptr->firsttime, sincetime,
                        IsServer(acptr) ? (DoesTS(acptr) ?
                        "TS" : "NoTS") : "-");
        }
        else
        {
            for (i = 0; i <= highest_fd; i++)
            {
                if (!(acptr = local[i]))
                    continue;
                if(!IsServer(acptr))
                    continue; /* nothing but servers */
#ifdef HIDEULINEDSERVS
                if(IsULine(acptr) && !IsAnOper(sptr))
                    continue;
#endif
                sincetime = (acptr->since > timeofday) ? 0 : 
                             timeofday - acptr->since;
                sendto_one(sptr, Lformat, me.name, RPL_STATSLINKINFO, parv[0],
                        get_client_name(acptr, HIDEME),
                        (int) DBufLength(&acptr->sendQ),
                        (int) acptr->sendM, (int) acptr->sendK,
                        (int) acptr->receiveM, (int) acptr->receiveK,
                        timeofday - acptr->firsttime, sincetime,
                        IsServer(acptr) ? (DoesTS(acptr) ?
                        "TS" : "NoTS") : "-");
            }
        }
        break;
        case 'C':
        case 'c':
        /* this should be fixed and combined into a more reasonable
         * single responce.  Will work on this later -epi
         * for now, assume all field are filled.  if they're not, 
         * we could be complaining at our config parser, not here.
         */
#ifdef HIDEULINEDSERVS
        if (!IsAnOper(sptr))
            sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name,  parv[0]);
        else
#endif
        {
            aConnect *tmp;
            if(!connects)
                break;
            for( tmp = connects ; tmp; tmp = tmp->next)
            {
                if(!IsAnOper(sptr) && !IsULine(sptr))
                {
                    sendto_one(sptr, rpl_str(RPL_STATSCLINE), me.name,
                                sptr->name, "C", "*", tmp->name, 
                                (tmp->port ? tmp->port : 0),
                                tmp->class->name);
                    sendto_one(sptr, rpl_str(RPL_STATSNLINE), me.name,
                                sptr->name, "N", "*", tmp->name, "1",
                                tmp->class->name);
                }
                else
                {
                    sendto_one(sptr, rpl_str(RPL_STATSCLINE), me.name,
                           sptr->name, "C", tmp->host, tmp->name,
                           (tmp->port ? tmp->port : 0), tmp->class->name);
                    sendto_one(sptr, rpl_str(RPL_STATSNLINE), me.name,
                           sptr->name, "N", tmp->host, tmp->name, "1",
                           tmp->class->name);
                }
            }
        }
        break;

        case 'G':
            report_simbans_match_flags(sptr, SBAN_GCOS|SBAN_LOCAL, 0);
            break;
        case 'g':
            report_simbans_match_flags(sptr, SBAN_GCOS|SBAN_NETWORK, 0);
            break;

        case 'I':
        case 'i':
        {
            aAllow *tmp;
            if(!allows)
                break;
            for(tmp = allows; tmp; tmp = tmp->next)
                sendto_one(sptr, rpl_str(RPL_STATSILINE), me.name,
                           sptr->name, "I", tmp->ipmask, tmp->hostmask,
                           tmp->port, tmp->class->name);
            break;
        }
        case 'k':
            if(IsAnOper(sptr))
                report_userbans_match_flags(sptr, UBAN_TEMPORARY|UBAN_LOCAL, 0);
            break;

        case 'K':
            if (IsAnOper(sptr))
                report_userbans_match_flags(sptr, UBAN_LOCAL, UBAN_TEMPORARY);
            break;

        case 'A':
        case 'a':
            if(IsAnOper(sptr))
                report_userbans_match_flags(sptr, UBAN_NETWORK, 0);
            break;

        case 'M':
        case 'm':
        /*
         * original behaviour was not to report the command, if
         * the command hadn't been used. I'm going to always
         * report the command instead -Dianora
         * Why would users want to see this?  Made Oper only.
         */
            if(IsAnOper(sptr))
                for (mptr = msgtab; mptr->cmd; mptr++)
                    sendto_one(sptr, rpl_str(RPL_STATSCOMMANDS), me.name, 
                            parv[0], mptr->cmd, mptr->count, mptr->bytes);
            break;

        case 'N':
        case 'n':
            sendto_one(sptr, rpl_str(RPL_STATSCOUNT), me.name, parv[0],
                        "User Connects Today: ", Count.today);
            sendto_one(sptr, rpl_str(RPL_STATSCOUNT), me.name, parv[0],
                        "User Connects past week: ", Count.weekly);
            sendto_one(sptr, rpl_str(RPL_STATSCOUNT), me.name, parv[0],
                        "User Connects past month: ", Count.monthly);
            sendto_one(sptr, rpl_str(RPL_STATSCOUNT), me.name, parv[0],
                        "User Connects past year: ", Count.yearly);
            break;
        case 'o':
        case 'O':
        {
            aOper *tmp;
            if(!opers)
                break;
            for(tmp = opers; tmp; tmp = tmp->next)
                sendto_one(sptr, rpl_str(RPL_STATSOLINE), me.name,
                            sptr->name, "O", tmp->hostmask, tmp->nick,
                            tmp->flags, tmp->class->name);
            break;
        }

        case 'p':
        case 'P':
            show_opers(sptr, parv[0]);
            break;

        case 'Q':
            report_simbans_match_flags(sptr, SBAN_NICK|SBAN_LOCAL, 0);
            report_simbans_match_flags(sptr, SBAN_CHAN|SBAN_LOCAL, 0);
            break;
        case 'q':
            report_simbans_match_flags(sptr, SBAN_NICK|SBAN_NETWORK, 0);
            report_simbans_match_flags(sptr, SBAN_CHAN|SBAN_NETWORK, 0);
            break;

        case 'R':
        case 'r':
#ifdef DEBUGMODE
            send_usage(sptr, parv[0]);
#endif
            break;

        case 'S':
        case 's':
            if (IsAnOper(sptr))
                list_scache(cptr, sptr, parc, parv);
            break;

        case 'T':
            if (IsAnOper(sptr)) 
                throttle_stats(sptr, parv[0]);
            break;

        case 't':
            if (IsAnOper(sptr))
                tstats(sptr, parv[0]);
            break;

        case 'U':
#ifdef HIDEULINEDSERVS
            if (!IsOper(sptr))
                sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name,  parv[0]);
            else
#endif
            {
                aUserv *tmp;
                if(!uservers)
                    break;
                for(tmp = uservers; tmp; tmp = tmp->next)
                    sendto_one(sptr, rpl_str(RPL_STATSULINE), me.name,
                    sptr->name, "U", "*", tmp->name, 0, 0);
            }
            break;

        case 'u':
        {
            time_t now;

            now = timeofday - me.since;
            sendto_one(sptr, rpl_str(RPL_STATSUPTIME), me.name, parv[0],
                now / 86400, (now / 3600) % 24, (now / 60) % 60, now % 60);
            break;
        }

        case 'v':
        case 'V':
            show_servers(sptr, parv[0]);
            break;

#ifdef DEBUGMODE
        case 'w':
        case 'W':
            if(IsAnOper(sptr))
                report_fds(sptr);
            break;
#endif

        case 'Y':
        case 'y':
        {
            aClass *tmp;
            if(!classes)
                break;
            for(tmp = classes; tmp; tmp = tmp->next)
                sendto_one(sptr, rpl_str(RPL_STATSYLINE), me.name,
                            sptr->name, 'Y', tmp->name, tmp->pingfreq,
                            tmp->connfreq, tmp->maxlinks, tmp->maxsendq);
            break;
        }

        case 'Z':
        case 'z':
            if (IsAnOper(sptr))
                count_memory(sptr, parv[0]);
            else
                sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
            break;

        case '?':
            serv_info(sptr, parv[0]);
            break;

        default:
            stat = '*';
            break;
    }
    sendto_one(sptr, rpl_str(RPL_ENDOFSTATS), me.name, parv[0], stat);
    return 0;
}
