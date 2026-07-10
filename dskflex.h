/* vim:ts=4
 * dskflex.h -- Flex floppy structures
   Copyright (C) 2025-2026 Michel Wurtz - mjwurtz@gmail.com

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

// Flex image file description

extern struct Disk {
    char *shortname;     // Name of image file
    char label[16];      // Flex disk label (11 char max)
    uint16_t volnum;     // Flex volume number (0-65535)
    uint8_t *dsk;        // Disk image in memory
    uint32_t size;       // Size of the image in bytes
    uint16_t nb_sectors; // Size of the image in sectors
    int fd;              // Image file descriptor
    uint8_t nbtrk;       // Number of tracks
    uint8_t nbsec;       // Number of sectors/track
    int freesec;         // Number of free sectors
    uint8_t readonly;    // Image file is readonly ? 
    int track0l;         // number of sectors on track 0
} disk;

// System Information record -- Not used yet
// Should be a pointer to Image file at offset 0x200
struct Sir {
    uint8_t padding[16];    // Should be 16 x 0
    uint8_t volname[11];    // Volume name
    uint8_t volnum[2];      // volume number (high, low byte order)
    uint8_t free_start_trk; // First free track/sector
    uint8_t free_start_sec;
    uint8_t free_end_trk;   // Last free track/sector
    uint8_t free_end_sec;
    uint8_t free_len[2];    // number of free sectors (high, low byte order)
    uint8_t create_date[3]; // MM-DD-YY in binary
                            // (add 1900 if YY > 75, else add 2000)
    uint8_t nbtrk;          // Highest track number (numbered 0 to nbtrk)
    uint8_t nbsec;          // Number of sectors/track (numbered 1 to nbsec)
};

// Directory sector description
//

struct Entry {          // File descriptor : 
    uint8_t name[8];    // 0x00 = filename
    uint8_t ext[3];     // 0x08 = extension
    uint8_t prot;       // 0x0B = protection on file :
                        //  --> $80 = Write protected
                        //  --> $40 = Delete protected
                        //  --> $10 = Catalog protected
    uint8_t notused;    // 0x0C = reserved for future use ?
    uint8_t first_trk;  // 0x0D = first track/sector address
    uint8_t first_sec;  // 0x0E
    uint8_t last_trk;   // 0x0F = last track/sector address
    uint8_t last_sec;   // 0x10
    uint8_t length[2];  // 0x11 = length of file in sectors
    uint8_t flags;      // 0x13 = 0x02 if random access file
    uint8_t fill;       // 0x14 = not used yet ?
    uint8_t f_month;    // 0x15 = month of creation (1-12)
    uint8_t f_day;      // 0x16 = day of creation (1-31)
    uint8_t f_year;     // 0x17 = year of creation (+ 1900 if >= 75, else +2000)
};        

extern struct Dirsec {
    uint8_t nxt_trk;        // next track/sector (bloc chaining)
    uint8_t nxt_sec;
    uint8_t ign[14];        // not used
    struct Entry entry[10]; // 10 entries per bloc
} *dirsec;

// table for files' analyse
extern struct File {
    uint8_t name[16];   // Name of file (8+3)
    int day;        // date of file 
    int month;
    int year;
    uint8_t start_trk;  // First track/sector of file
    uint8_t start_sec;
    uint8_t end_trk;    // Last track/sector of file
    uint8_t end_sec;
    int length;         // Length of file (in sectors)
    int random;         // File is random
    int flags;            
    uint8_t *pos;
} *file;

extern char *s_err, *s_warn, *s_norm; // for color messages

extern uint8_t lasttrk, lastsec;      // last track/sector on disk
extern int nfile;                     // number of files
extern int nslot;                     // number of slots (file emplacement)
extern int usedsec;                   // number of used files sectors
extern int notused;                   // Number of blocs not reclaimed (trk 1-n)
extern int notdir;                    // Number of dir blocs not used (trk 0)
extern int ndel;                      // Number of files deleted

extern int quiet;
extern int verbose;

// external functions
extern int isFlex( uint8_t *dsk, long nbsect);
extern int badFlex( int strict);    // strict = 1 => abort if image not clean
extern int ts2blk( uint8_t ntrk, uint8_t nsec);
extern uint8_t blk2trk( int index);
extern uint8_t blk2sec( int index);
extern uint8_t *ts2pos( uint8_t ntrk, uint8_t nsec);
extern int getname( uint8_t *pos, char *name, int dot);
extern int analyse( int strict);
extern int list_files( int details);

// static char *month[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

extern int *nxtsec;    // table for sector linking
extern int *tabsec;    // table for sector usage

