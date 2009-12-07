/*
   Copyright 2008 Gemius SA.

   This file is part of MooseFS.

   MooseFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   MooseFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with MooseFS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/time.h>
#include <errno.h>

#include "charts.h"
#include "main.h"

#include "chunks.h"
#include "filesystem.h"

#define CHARTS_FILENAME "stats.mfs"

#define CHARTS_UCPU 0
#define CHARTS_SCPU 1
#define CHARTS_DELCHUNK 2
#define CHARTS_REPLCHUNK 3
#define CHARTS_STATFS 4
#define CHARTS_GETATTR 5
#define CHARTS_SETATTR 6
#define CHARTS_LOOKUP 7
#define CHARTS_MKDIR 8
#define CHARTS_RMDIR 9
#define CHARTS_SYMLINK 10
#define CHARTS_READLINK 11
#define CHARTS_MKNOD 12
#define CHARTS_UNLINK 13
#define CHARTS_RENAME 14
#define CHARTS_LINK 15
#define CHARTS_READDIR 16
#define CHARTS_OPEN 17
#define CHARTS_READ 18
#define CHARTS_WRITE 19

#define CHARTS 20

/* name , join mode , percent , scale , multiplier , divisor */
#define STATDEFS { \
	{"ucpu"         ,CHARTS_MODE_ADD,1,CHARTS_SCALE_MICRO, 100,60}, \
	{"scpu"         ,CHARTS_MODE_ADD,1,CHARTS_SCALE_MICRO, 100,60}, \
	{"delete"       ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"replicate"    ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"statfs"       ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"getattr"      ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"setattr"      ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"lookup"       ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"mkdir"        ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"rmdir"        ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"symlink"      ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"readlink"     ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"mknod"        ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"unlink"       ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"rename"       ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"link"         ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"readdir"      ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"open"         ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"read"         ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{"write"        ,CHARTS_MODE_ADD,0,CHARTS_SCALE_NONE ,   1, 1}, \
	{NULL           ,0              ,0,0                 ,   0, 0}  \
};

#define CALCDEFS { \
	CHARTS_DEFS_END \
};

/* c1_def , c2_def , c3_def , join mode , percent , scale , multiplier , divisor */
#define ESTATDEFS { \
	{CHARTS_DIRECT(CHARTS_UCPU)        ,CHARTS_DIRECT(CHARTS_SCPU)        ,CHARTS_NONE                       ,CHARTS_MODE_ADD,1,CHARTS_SCALE_MICRO, 100,60}, \
	{CHARTS_NONE                       ,CHARTS_NONE                       ,CHARTS_NONE                       ,0              ,0,0                 ,   0, 0}  \
};

static const uint32_t calcdefs[]=CALCDEFS
static const statdef statdefs[]=STATDEFS
static const estatdef estatdefs[]=ESTATDEFS

static struct itimerval it_set;

void chartsdata_refresh(void) {
	uint64_t data[CHARTS];
	uint32_t fsdata[16];
	uint32_t i,del,repl; //,bin,bout,opr,opw,dbr,dbw,dopr,dopw,repl;
	struct itimerval uc,pc;
	uint32_t ucusec,pcusec;

	for (i=0 ; i<CHARTS ; i++) {
		data[i]=0;
	}

	setitimer(ITIMER_VIRTUAL,&it_set,&uc);             // user time
	setitimer(ITIMER_PROF,&it_set,&pc);                // user time + system time

	if (uc.it_value.tv_sec<=999) {	// on fucken linux timers can go backward !!!
		uc.it_value.tv_sec = 999-uc.it_value.tv_sec;
		uc.it_value.tv_usec = 999999-uc.it_value.tv_usec;
	} else {
		uc.it_value.tv_sec = 0;
		uc.it_value.tv_usec = 0;
	}
	if (pc.it_value.tv_sec<=999) {	// as abowe - who the hell has invented this stupid os !!!
		pc.it_value.tv_sec = 999-pc.it_value.tv_sec;
		pc.it_value.tv_usec = 999999-pc.it_value.tv_usec;
	} else {
		pc.it_value.tv_sec = 0;
		uc.it_value.tv_usec = 0;
	}

	ucusec = uc.it_value.tv_sec*1000000+uc.it_value.tv_usec;
	pcusec = pc.it_value.tv_sec*1000000+pc.it_value.tv_usec;

	if (pcusec>ucusec) {
		pcusec-=ucusec;
	} else {
		pcusec=0;
	}
	data[CHARTS_UCPU] = ucusec;
	data[CHARTS_SCPU] = pcusec;

	chunk_stats(&del,&repl);
	data[CHARTS_DELCHUNK]=del;
	data[CHARTS_REPLCHUNK]=repl;
	fs_stats(fsdata);
	for (i=0 ; i<16 ; i++) {
		data[CHARTS_STATFS+i]=fsdata[i];
	}

	charts_add(data,main_time()-60);
}

void chartsdata_term(void) {
	chartsdata_refresh();
	charts_store(NULL);
}

void chartsdata_store(void) {
	charts_store(NULL);
}

int chartsdata_init (FILE *msgfd) {
	struct itimerval uc,pc;

	it_set.it_interval.tv_sec = 0;
	it_set.it_interval.tv_usec = 0;
	it_set.it_value.tv_sec = 999;
	it_set.it_value.tv_usec = 999999;
	setitimer(ITIMER_VIRTUAL,&it_set,&uc);             // user time
	setitimer(ITIMER_PROF,&it_set,&pc);                // user time + system time

	main_timeregister(TIMEMODE_RUNONCE,60,0,chartsdata_refresh);
	main_timeregister(TIMEMODE_RUNONCE,3600,0,chartsdata_store);
	main_destructregister(chartsdata_term);
	return charts_init(calcdefs,statdefs,estatdefs,CHARTS_FILENAME,msgfd);
}