/* dsk.h -- Flex floppy structures 
   Copyright (C) 2022 Michel Wurtz - mjwurtz@gmail.com

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#define _XOPEN_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>

// Sector size for Flex floppy
#define SECSIZE 256
#define DIRSIZE 2560  // Max value for a $FF sectors/track dir on track 0
					  // only. Must be changed for a dynamic mem allocation

static struct Disk {
	char filename[256];
	char *shortname;
	char label[16];
	uint16_t volnum;
	uint8_t *dsk;
	uint32_t size;
	int fd;
	uint8_t nbtrk;
	uint8_t nbsec;
	int freesec;
	uint8_t track_id;
	uint8_t readonly;
	int track0l;	// length of track 0 in sectors
} disk;

static struct File {
	char name[16];
	time_t mtime;
	uint8_t start_trk;
	uint8_t start_sec;
	uint8_t end_trk;
	uint8_t end_sec;
	int length;
	int random;
	int flags;
	char *pos;
} file[DIRSIZE];

static int nfile, nslot;
static int usedsec = 0;

static char *month[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

static int *nxtsec;	// table for sector linking
static int *tabsec;	// table for sector usage

// Convert track/sector to bloc number on the disc image 
int ts2blk( uint8_t ntrk, uint8_t nsec) {
	if (ntrk > disk.nbtrk || nsec > disk.nbsec || (nsec == 0 && ntrk != 0)) {
		return( -1);
	}
	if (ntrk == 0) {	// track 0 is straitforward...
	  if (nsec == 0) {	// but sector=0 is not an error but a special case...
		return 0;
	  } else {
		return (nsec - 1);
	  }
	} else {
		return disk.track0l + (ntrk - 1) * disk.nbsec + nsec - 1;
	}
}

// Convert bloc number on the disc image to track number
uint8_t blk2trk( int index) {
	if( index < disk.track0l)
		return 0;
	else
		return (index - disk.track0l) / disk.nbsec + 1;
}

// Convert bloc number on the disc image to sector number 
uint8_t blk2sec( int index) {
	if( index < disk.track0l)
		return index+1;
	else
		return (index - disk.track0l) % disk.nbsec + 1;
}

// Convert track/sector pointer on disk image

uint8_t *ts2pos( uint8_t ntrk, uint8_t nsec) {
  int pos;
	if ((pos = ts2blk( ntrk, nsec)) < 0)
		return NULL;
	return disk.dsk + pos * SECSIZE;
}

// read 8+3 name and insert a dot if necessary
// return the number of char of the name
// dot = 0 for volume name (11 chars, no ext.)
// return the length of name, parameter name updated

int getname( uint8_t *pos, char *name, int dot) {
  int k = 0;
  for (int j=0; j<11; j++) {
	if (!isalnum(pos[j]) && pos[j] != '-' && pos[j] != '_' && pos[j] != 0xFF
			&& pos[j] != ' ' && pos[j] != '*' && pos[j] != '.' && pos[j] != 0)
	  return -1;
	if (dot && pos[j] == ' ')
	  return -1;
	if (pos[j] != 0 )
	  name[k++] = pos[j];
	if (j == 7)	{
	  if (pos[8] == 0)
		break;
	  if (dot)
		name[k++] = '.';
	}
  }
  name[k] = 0;
  return k;
}

