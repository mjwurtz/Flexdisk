/* tstflex.c -- Flex image validation 
   Copyright (C) 2022-2026 Michel Wurtz - mjwurtz@gmail.com

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

#include "dskflex.h"

// Some global variables for managing flex image

struct Disk disk;
struct File *file;
uint8_t lasttrk, lastsec;  // last track/sector on disk

int nfile;  	// number of valid entries in directory
int nslot;		// size of directory (max number of entries)
int usedsec;	
int notused;
int notdir;
int ndel;		// Number of files deleted

int *nxtsec;	// table for sector linking
int *tabsec;	// table for sector usage

struct Dirsec *dirsec;

///////////////////////////////
// Is it a Flex disk image ? //
// Return 1 if yes, 0 if not //
///////////////////////////////

int isFlex( uint8_t *dsk, long nsect) {

  long size;       // size calculated if not Flex

  disk.nb_sectors = disk.size / SECSIZE;
  if (!quiet) {
    printf( "File name: %s\n", disk.shortname);
    printf( "Physical number of sectors: %u\n", disk.size / SECSIZE);
    if (disk.nb_sectors * SECSIZE != disk.size) {
      printf( "[disk size doesn't match an integer number of sectors: %u bytes left]\n",
        disk.size % SECSIZE);
      return 0;
    }
  }

  if (getname( dsk + 0x210, disk.label, 0) < 0 ||
      dsk[0x226] == 0 || dsk[0x227] == 0) {
    printf( "Not a Flex disk image: ");

// Test OS/9
    size = (((long)dsk[0]*256)+(long)dsk[1])*256 + (long)dsk[2];
    if (size == nsect) {
      printf( "Probably an OS-9 image...\n");
    } else {

// Test Uniflex
      size = (((long)dsk[0x212]*256) + (long)dsk[0x213] +
      (long)dsk[0x23F])*256 + (long)dsk[0X214] + (long)dsk[0X240] + 1;
      if (size * 2 == nsect)
        printf( "Probably an UniFLEX image...\n");

// Test FDOS
      else if (nsect == 350 && dsk[0x1400] == '$' && dsk[0x1401] == 'D'
                && dsk[0x1402] == 'O' && dsk[0x1403] == 'S')
        printf( "SWTPC 6800 FDOS image (35 tracks of 10 sectors) detected.\n");
      else // It's something other...
        printf( "Unknown disk image type\n");
    }
    return 0;
  }
  return 1;
}

//////////////////////////////////////
// Is the file a valid Flex Image ? //
// Test disk geometry and SIR ptrs  //
// Auto-detect SD/DD disk images    //
//////////////////////////////////////

int badFlex( int strict) {

  uint8_t last_trk_sec; // nb of sectors for last track if incomplete
  int retval = 0;       // value returned if problem detected
                        // 0: everything is ok; 1: freelist uncomplete/damaged
                        // 2: disk structure too damaged to continue
  
  disk.volnum = disk.dsk[0x21b]*256 + disk.dsk[0x21c];
  // Size of disk & free sector list
  disk.nbtrk = disk.dsk[0x226];
  disk.nbsec = disk.dsk[0x227];
  disk.freesec = disk.dsk[0x221]*256 + disk.dsk[0x222];

  // Too much free sectors for the disk ?
  if (disk.freesec > disk.nbtrk * disk.nbsec) {
    if (strict) {
      printf( "%sError: Number of free sectors bigger than disk size%s\n", s_err, s_norm);
      retval = 1;
    }
  }

// Print info about the disk if asked
  if (!quiet | verbose) {
    printf( "Flex Volume name: '%s', %d tracks, %d sectors/track\n",
            disk.label, disk.nbtrk+1, disk.nbsec);
    printf( "Flex Volume number: %d\n", disk.volnum);
    printf( "Creation Date (YYYY-MM-DD): %d-%02d-%02d\n",
            disk.dsk[0x225]>75?disk.dsk[0x225]+1900:disk.dsk[0x225]+2000,
            (disk.dsk[0x223]&0x0f)+1, disk.dsk[0x224]);
    printf( "Highest Sector address on disk: %02X/%02X\n",
            disk.dsk[0x226], disk.dsk[0x227]);
  }

// Try to guess disk geometry
  if ((disk.nbtrk+1) * disk.nbsec == disk.nb_sectors) {
    if (!quiet) {
      if (disk.nbsec > 18)
        printf( "Looks like a Gotek image or a DD disk with a DD track 0\n");
      else
        printf( "Looks like a Single Density disk\n");
    }
    disk.track0l = disk.nbsec;
  } else {
    disk.track0l = disk.nb_sectors - disk.nbtrk * disk.nbsec;
    if ((disk.nbsec >= 36 && disk.track0l == 20) ||
       (disk.nbsec == 18 && disk.track0l == 10) ||
       (disk.track0l == disk.nbsec/2)) {
      if (!quiet) {
        printf( "Looks like a Double Density disk with Single Density");
        printf( " track 0 of %d sectors\n", disk.track0l);
      }
    } else if (disk.track0l > disk.nbsec) {
// Weird geometry... but can happen when disks are in EEPROM
      if (strict || ! quiet) {
        printf( "%sUnknown geometry: %d tracks of %d", s_err, disk.nbtrk, disk.nbsec);
        printf( " sectors and a first track of %d sectors !%s\n", disk.track0l, s_norm);
      }
      if (strict)
        return 2;
      disk.track0l = disk.nbsec;
      disk.nbtrk++;
      last_trk_sec = disk.nb_sectors - (disk.nbtrk-1)*disk.nbsec - disk.track0l;
      if (!quiet | verbose) {
        printf( "%s  => Using normal %d sector track 0,", s_warn, disk.track0l);
        printf( " add a %d%s incomplete track of %d sectors%s\n",
          disk.nbtrk, "th", last_trk_sec, s_norm);
        }
    } else if (disk.nbsec > 10 && disk.track0l > disk.nbsec/2 && disk.track0l < disk.nbsec) {
      if(!quiet | verbose) {
        printf( "Looks like a Double Density disk with Single Density");
        printf( " track 0 of %d sectors\n", disk.track0l);
      }
    } else {
// This is generaly no good, trying to guess end of track 0
      disk.nbtrk -= (disk.nbtrk - disk.nb_sectors/disk.nbsec);
      if (!quiet | verbose) {
        printf( "%sERROR: Disk image too small... truncated ?\n", s_err);
        printf( "%sReducing number of tracks to %d, %s",
          s_warn, disk.nbtrk+1, s_norm);
      }
      if (disk.nbsec < 25)
        disk.track0l = disk.nbsec;
      else
        disk.track0l = disk.nb_sectors - disk.nbtrk * disk.nbsec;
// Try the highest probability value
      if (!quiet | verbose)
        printf( "%strying with first track of %d sectors%s\n",
          s_warn, disk.track0l, s_norm);
      retval = 257;
    }
  }

// Print general stats
  if (!quiet | verbose) {
    printf( "Number of data sectors: %d\n", disk.nb_sectors - disk.track0l);
    if (disk.freesec == 0)
      printf( "%sWarning: empty free list (no more space on disk)%s\n",
        s_warn, s_norm);
    else
      printf( "Free sectors: %d [%02X/%02X - %02X/%02X]\n", disk.freesec,
        disk.dsk[0x21d], disk.dsk[0x21e], disk.dsk[0x21f], disk.dsk[0x220]);
  }

  return retval;
}

/////////////////////////////////////////////////////
// Sanity check on disk image :                    //
// test chaining and free sectors list coherence   //
// test directory content and file's chained lists //
/////////////////////////////////////////////////////

int analyse( int strict) {

  int dirsize;            // directory max size in files
  int nbdirsec;           // number of directory's sectors not on track 0

  uint8_t *psec, *pbloc;  // pointers for bloc navigation
  int obloc, ibloc;       // bloc index for navigation
  int j, k;               // loop index / counter
  int nb_blk;             // nb of blocs used by a file (computed)
  char name[16];          // temp file name

  int retval = 0;         // return value (0 if OK)

// table of all blocs of the disk:
// tabsec may contain :
// * the directory entry number of the file using it
// * 0 for a directory bloc
// * -1 for a sector in freelist
// * -99999 for a bloc not reclaimed by a file
// * -99998 for a bloc not reclaimed on track 0 (directory)
  tabsec = malloc( sizeof(int) * disk.nb_sectors);
  nxtsec = malloc( sizeof(int) * disk.nb_sectors);

  for (ibloc = 0; ibloc < disk.nb_sectors; ibloc++) {
    if (ibloc < disk.track0l)
      tabsec[ibloc] = -99998; // directory blocs
    else
      tabsec[ibloc] = -99999;
    if (ibloc <= 2) {
        nxtsec[ibloc] = 0;
        continue;
    }
    if ((nxtsec[ibloc] = ts2blk( disk.dsk[ibloc*SECSIZE], disk.dsk[ibloc*SECSIZE+1])) < 0) {
      printf( "%sERROR: sector %d link out of bounds [0x%02X/0x%02X]%s\n",
        s_err, ibloc, disk.dsk[ibloc*SECSIZE], disk.dsk[ibloc*SECSIZE+1], s_norm);
      retval = 1;
      nxtsec[ibloc] = 0;              // sanitize to avoid further errors
	  disk.dsk[ibloc*SECSIZE] = 0;    // Useful for disk reparation
      disk.dsk[ibloc*SECSIZE+1] = 0;
    }
  }

// verifying freelist blocs
  k = 0;
  ibloc = ts2blk( disk.dsk[0x21d], disk.dsk[0x21e]);

  while (ibloc != 0 && ibloc != -1) {
    k++;
    if (tabsec[ibloc] == -1) { // duplicate sector in freelist
      if (strict)
        fprintf( stderr, "ERROR: Bad freelist, bloc %d duplicated\n", ibloc);
      retval = 1;
    }
    if (tabsec[ibloc] == -99998 && strict) {
      printf( "%sWarning: freelist contains track 0 bloc %d%s\n",
      s_warn, ibloc, s_norm);
    }

    tabsec[ibloc] = -1;
    obloc = ibloc;
    ibloc = nxtsec[ibloc];
  }

  if (k != disk.freesec) { // bad size
    if (strict | !quiet) {
      printf( "%sWarning: free sectors chain contains %d sectors", s_warn, k);
      printf( " instead of %d%s\n", disk.freesec, s_norm);
    }
    retval = 1;      
  }

  lasttrk = blk2trk(obloc);
  lastsec = blk2sec(obloc);
  if ( lasttrk != disk.dsk[0x21f] || lastsec != disk.dsk[0x220]) {
    if (strict | !quiet)
      printf( "%sWARNING: bad free list end [0x%02X/0x%02X] instead of [0x%02X/0x%02X]%s\n",
        s_warn, disk.dsk[0x21f], disk.dsk[0x220], lasttrk, lastsec, s_norm);
    retval = 1;      
  }

// Verifying directory
// First count the dir blocs to allocate file table space
// Reserve space for a complete track 0 directory if the
// number of directory blocs linked is less

  dirsize = 0;
  ibloc = ts2blk( 0, 5);
  do {
    if (tabsec[ibloc] < -1) {
      tabsec[ibloc] = 0;
      dirsize += 10;
    } else {
      if (tabsec[ibloc] == -1) {
        retval = 1 + strict;
        tabsec[ibloc] = 0;
        if (strict )
          printf( "%sERROR: Directory sector %d [0x%02X/0x%02X] also in freelist%s\n",
            s_err, ibloc, blk2trk( ibloc), blk2sec( ibloc), s_norm);
      } else {
        printf( "%sERROR: Directory sector %d [0x%02X/0x%02X] used twice (loop)%s\n",
          s_err, ibloc, blk2trk( ibloc), blk2sec( ibloc), s_norm);
        return 3; // No need to go further !
      }
    }
    ibloc = nxtsec[ibloc];
  } while (ibloc != 0);

  if (dirsize < (disk.track0l-2) * 10)
    dirsize = (disk.track0l-2) * 10;

  file = malloc( sizeof( struct File) * dirsize);
  if (file == NULL) {
    perror( "file table allocation failed");
    return 3;
  }

// File linking analyse and feature extraction...

  nfile = 0;
  nslot = 0;
  usedsec = 0;
  nbdirsec = 0;

  ibloc = ts2blk( 0, 5); // dir starts on sector 5 (offset = 0x400)

  while (nslot < dirsize) {
    if (ibloc >= disk.track0l) // directory bloc ouside track 0
      nbdirsec++;
    dirsec = (struct Dirsec*)(disk.dsk + ibloc * SECSIZE);
    for (k=0; k < 10 && nslot < dirsize; k++) {
      file[nslot].pos = dirsec->entry[k].name;
      if (dirsec->entry[k].name[0] == 0) {
        file[nslot].flags = 0;
		file[nslot].name[0] = 0;
		nslot++;
		continue;
	  }
      file[nslot].flags = 1;
      if (dirsec->entry[k].f_day < 1 || dirsec->entry[k].f_day > 31)
        file[nslot].day = 0;
      else
        file[nslot].day = (int)dirsec->entry[k].f_day;
      if (dirsec->entry[k].f_month < 1 || dirsec->entry[k].f_month > 12)
        file[nslot].month = 0;
      else
        file[nslot].month = (int)dirsec->entry[k].f_month;
      if (dirsec->entry[k].f_year > 75)
        file[nslot].year = (int)dirsec->entry[k].f_year + 1900;
      else
        file[nslot].year = (int)dirsec->entry[k].f_year + 2000;

      if (dirsec->entry[k].flags) {  // random file
        file[nslot].flags |= 2;
      }
      file[nslot].length = dirsec->entry[k].length[0]*256+dirsec->entry[k].length[1];

//Name valid ?
      if (getname( dirsec->entry[k].name, name, 1) < 0) {
        printf( "%sERROR: Directory entry %d : name not valid%s\n",
          s_err, nslot, s_norm);
        file[nslot].flags |= 0x40;
      }
	  if (*name == 0)
	    strcpy( name, "???");
      strcpy( file[nslot].name, name);
      file[nslot].start_trk = dirsec->entry[k].first_trk;
      file[nslot].start_sec = dirsec->entry[k].first_sec;
      file[nslot].end_trk   = dirsec->entry[k].last_trk;
      file[nslot].end_sec   = dirsec->entry[k].last_sec;

// first sector valid ?
      j = ts2blk( dirsec->entry[k].first_trk, dirsec->entry[k].first_sec);
      if ((j < 1 || j == 2) && *name != 0xFF && dirsec->entry[k].length[2] != 0) { 
        printf( "%sERROR: Directory entry %d (%s) : sector [%02X,%02X] not valid%s\n",
          s_err, nslot, name, dirsec->entry[k].first_trk, dirsec->entry[k].first_sec, s_norm);
        file[nslot].length = 0;
        file[nslot].flags |= 0x80;
        retval = 2;
        continue;
      }
// Deleted file ?
      if (dirsec->entry[k].name[0] == 0xFF) {
        name[0] = '?';
        ndel++;
        file[nslot].flags |= 0x10;
      } else {
	    nfile++;
        usedsec += file[nslot].length;
      }
      nslot++;
    }

    // End of directory blocs ?
    if ((ibloc = nxtsec[ibloc]) == 0)
      break;;
  }

// File's blocs linking analyse...
// First pass ignore deleted files

  for( k=0; k < nslot; k++) {
	ibloc = ts2blk( file[k].start_trk, file[k].start_sec);

	if (ibloc < 0) {
	  printf( "%sERROR: File %s (%d), first sector [0x%02X/0x%02X] out of bounds%s\n",
        s_err, file[k].name, k+1, file[k].start_trk, file[k].start_sec, s_norm);
      retval = 1;
      file[k].flags = 0;  // Ignore this file
	  continue;
	}

	if ((file[k].flags & 0x11) != 1)
	  continue;
	
    nb_blk = 0;
	while (ibloc) { // Valid <= chaining verified before
	  if (tabsec[ibloc] <= -1) {
	    if (tabsec[ibloc] == -1) {
          printf( "%sERROR: File %s (%d), sector [0x%02X/0x%02X] also in freelist%s\n",
            s_err, file[k].name, k+1, blk2trk(ibloc), blk2sec( ibloc), s_norm);
          file[k].flags |= 0x40;
          retval = 1;
		}
	    tabsec[ibloc] = k+1;
        nb_blk++;
      } else if (tabsec[ibloc] == 0) {
        printf( "%sERROR: File %s (%d), sector [0x%02X/0x%02X] also in directory%s\n",
          s_err, file[k].name, k+1, blk2trk(ibloc), blk2sec( ibloc), s_norm);
        file[k].flags |= 0x80;
        retval = 2;
		break;
      } else { 
        printf( "%sERROR: File %s (%d), sector [0x%02X/0x%02X] also in file %s (%d)%s\n",
          s_err, file[k].name, k+1, blk2trk(ibloc), blk2sec( ibloc), file[tabsec[ibloc]].name, tabsec[ibloc], s_norm);
        file[k].flags |= 0x80;
      }
	  obloc = ibloc;
      ibloc = nxtsec[ibloc];
	}
    if (nb_blk != file[k].length) {
      printf( "%sERROR: length of %s %d, but %d sectors chained%s\n",
        s_err, file[k].name, file[k].length, nb_blk, s_norm);
      file[k].flags |= 0x40;
    }
	if ((blk2trk( obloc) != file[k].end_trk || blk2sec( obloc) != file[k].end_sec) &&
	   file[k].length != 0) {
	  printf( "%sERROR: last track/sector don't match [0x%02X/0x%02X] vs [0x%02X/0x%02X]%s\n",
		s_err, blk2trk( obloc), blk2sec( obloc), file[k].end_trk, file[k].end_sec, s_norm);
	  retval = strict + 1;
	  file[k].flags |= 0x80;
	}
  }	  

  // Scan for Deleted file

  for( k=0; k < nslot; k++) {
    if (file[k].flags & 0x10) {
      file[k].flags |= 0x20;        // We hope it can be restored
	  ibloc = ts2blk( file[k].start_trk, file[k].start_sec);
      for (j = 0; j < file[k].length; j++) {
        if (ibloc < 0) {            // nope
          file[k].flags &= 0xdf;
          break;
        }
        if (tabsec[ibloc] >= 0) {    // nope
          file[k].flags &= 0xdf;
          break;
        }
		obloc = ibloc;
        ibloc = nxtsec[ibloc];
      }
      if (j != file[k].length && obloc != ts2blk( file[k].end_trk, file[k].end_sec))
          file[k].flags &= 0xdf;    //nope
    }
  }

  if (nbdirsec) {
    printf( "%sWarning: %d sectors used by directory outside track 0%s\n",
      s_warn, nbdirsec, s_norm);
  }

  notused = 0; // Number of blocs not reclaimed (trk 1-n)
  notdir = 0;  // Number of dir blocs not used (trk 0)
  for (k=5; k < disk.track0l; k++) {
    if (tabsec[k] == -99998) {
      notdir++;
    }
  }
  for (k=disk.track0l; k < disk.nb_sectors; k++) {
    if (tabsec[k] == -99999) {
      notused++;
    }
  }
  if (notdir) {
    if (retval < 1)
      retval = 1;
    if (!quiet) {
      printf( "%sWarning : %d directory sector(s) not linked in track 0%s\n", s_warn, notdir, s_norm);
      for (k=5; k < disk.track0l; k++)
        if (verbose && (tabsec[k] == -99998))
            printf( "[%02X/%02X]   ", blk2trk( k), blk2sec( k));
      putchar( '\n');
    }
  }
  if (notused) {
    if (retval < 1)
      retval = 1;
    if (!quiet) {
      printf( "%sWarning : %d sector(s) missing in freelist%s\n", s_warn, notused, s_norm);
      for (k=5; k < disk.nb_sectors; k++)
        if (tabsec[k] == -99999)
          if (verbose)
            printf( "[%02X/%02X]   ", blk2trk( k), blk2sec( k));
      putchar( '\n');
    }
  }

  return retval;
}

///////////////////////////////////////////////////////////
// Convert track/sector to bloc number on the disc image //
// Return -1 if track or sector number out of bound      //
///////////////////////////////////////////////////////////

int ts2blk( uint8_t ntrk, uint8_t nsec) {
    if (ntrk > disk.nbtrk || nsec > disk.nbsec || (nsec == 0 && ntrk != 0)) {
        return( -1);
    }
    if (ntrk == 0) {    // track 0 is straitforward...
      if (nsec == 0) {    // but sector=0 is not an error but a special case...
        return 0;
      } else {
        return (nsec - 1);
      }
    } else {
        return disk.track0l + (ntrk - 1) * disk.nbsec + nsec - 1;
    }
}

///////////////////////////////////////////////////////////
// Convert bloc number on the disc image to track number //
///////////////////////////////////////////////////////////

uint8_t blk2trk( int index) {
    if( index < disk.track0l)
        return 0;
    else
        return (index - disk.track0l) / disk.nbsec + 1;
}

////////////////////////////////////////////////////////////
// Convert bloc number on the disc image to sector number //
////////////////////////////////////////////////////////////

uint8_t blk2sec( int index) {
    if (index == 0)
        return 0;
    if (index < disk.track0l)
        return index+1;
    else
        return (index - disk.track0l) % disk.nbsec + 1;
}

////////////////////////////////////////////////
// Convert track/sector pointer on disk image //
////////////////////////////////////////////////

uint8_t *ts2pos( uint8_t ntrk, uint8_t nsec) {
  int pos;
    if ((pos = ts2blk( ntrk, nsec)) < 0)
        return NULL;
    return disk.dsk + pos * SECSIZE;
}

///////////////////////////////////////////////////////
// read 8+3 name and insert a dot if necessary       //
// return the number of char of the name             //
// dot = 0 for volume name (11 chars, no ext.)       //
// return the length of name, parameter name updated //
// return -1 if the name is illegal for Flex         //
///////////////////////////////////////////////////////

int getname( uint8_t *pos, char *name, int dot) {
  int k = 0;
  for (int i=0; i<11; i++) {
    if (!isalnum(pos[i]) && pos[i] != '-' && pos[i] != '_' && pos[i] != 0xFF
            && pos[i] != ' ' && pos[i] != '*' && pos[i] != '.' && pos[i] != 0)
      return -1;
    if (dot && pos[i] == ' ')
      return -1;
    if (pos[i] != 0 )
      name[k++] = pos[i];
    else if (dot == 0)
      break;
    if (i == 7)    {
      if (pos[8] == 0)
        break;
      if (dot)
        name[k++] = '.';
    }
  }
  name[k] = 0;
  return k;
}

//////////////////////////////////////////////////////
// Print a short or long list of files after action //
//////////////////////////////////////////////////////

int list_files( int details) {
  int j, k;  // index for loops

  if (nfile > 0) {
    if (details) {
      printf( "\nFile list (%d used + %d deleted / %d entries):\n",
              nfile, ndel, nslot);
      printf( " id       Filename  start    end    size      date    flags\n");
      for( k = 0; k < nslot; k++) {
	    if ((file[k].flags & 0x01) != 1)
		  continue;
        printf( "%3d %14s [%02X/%02X - %02X/%02X] %5d",
          k+1, file[k].name, file[k].start_trk, file[k].start_sec,
          file[k].end_trk, file[k].end_sec, file[k].length);
        printf( "   %d-%02d-%02d", file[k].year, file[k].month, file[k].day);
        if (file[k].flags & 2)
            printf( " random access");
        if (file[k].flags & 0x10)
          printf( " DELETED");
        if (file[k].flags & 0x80)
          printf( " %sCORRUPTED%s", s_err, s_norm);
        if (file[k].flags & 0x40)
          printf( " %sUNUSABLE%s", s_warn, s_norm);
        if (file[k].flags & 0x20)
          printf( " (maybe recoverable)");
        putchar( '\n');
      }
    } else {
      printf( "\nFile list (%d/%d):\n", nfile, nslot);
      for( j = 0, k = 0; k < nslot; k++)
        if ((file[k].flags & 0x01) && (file[k].flags & 0x10) == 0) {
          printf( "%12s    ", file[k].name);
          if ((++j % 5) == 0)
            putchar( '\n');
        }
      putchar( '\n');
    }
  } else
    printf( "\nEmpty directory (%d entries).\n", nslot);
}
