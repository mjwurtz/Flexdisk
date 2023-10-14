/* flan.c -- Flex floppy analyser 
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

#include "dsk.h"

// Some global variables
static int retval = 0;
static int verbose = 0;
static int quiet = 0;
static int repar = 0;

char *s_err, *s_warn, *s_norm;

// Help message
void usage( char *cmd) {
    fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
    fprintf( stderr, "       %s [-q|-v] [-r] <file>\n", cmd);
    fprintf( stderr, "Options:\n");
    fprintf( stderr, "   -q => quiet, don't print anything\n");
    fprintf( stderr, "   -r => repair and/or reorder free sector list\n");
    fprintf( stderr, "   -v => print a detailled listing of files\n");
}

// Display directory of a 6800 FDOS disk

void fdosdir() {
  uint8_t *entry;

  printf( "SWTPC 6800 FDOS disk (35 tracks of 10 sectors) detected.\n\n");
  printf( "Name     Password  start  size type load at  end  start @\n");
  printf( "-------- -------- ------ ----- ---- ------- ----- -------\n");
  entry = disk.dsk + 0x1400;
  while (*entry != 0xFF) {
    for (int i = 0; i < 16; i++)
      putchar( entry[i]);
    printf( "  [%02X/%02X]", entry[16], entry[17]);
    printf( " %3d", (int)entry[18]*256 + (int)entry[19]);
    printf( "   %02X", entry[20]);
    printf( "   $%02X%02X", entry[21], entry[22]);
    printf( "  $%02X%02X", entry[23], entry[24]);
    printf( "  $%02X%02X\n", entry[25], entry[26  ]);
    entry += 32;
  }
  printf( "\nFree space start [%02X/%02X], length %d\n",
    entry[16], entry[17], (int)entry[18]*256 + (int)entry[19]);
}

// Analyse the content of the disk loaded

int browse_dsk() {

  uint8_t *current_sector, *dsk, *dir, *base, *entry, cftrk, cfsec;
  int sector_size = SECSIZE;
  int nb_sectors;
  int dirsec;    // Number of directory sectors outside track 0
  int ibloc, freediff;
  int i, j, k;
  char name[16];
  int ndel; // Number of files deleted
  struct tm dsktime, *ftime;
  uint8_t last_trk_sec;
 
  int free_nb, reorg, free_start; // For free list reorganisation

  nb_sectors = disk.size / SECSIZE;
  if (!quiet) {
    printf( "File name: %s\n", disk.shortname);
    printf( "Physical number of sectors: %u\n", disk.size / SECSIZE);
    if (nb_sectors * SECSIZE != disk.size)
      printf( "[disk size don't match an integer number of sectors: %u bytes left]\n",
        disk.size % SECSIZE);
  }

// Not a flex disk ?
  if (getname( disk.dsk + 0x210, disk.label, 0) < 0 ||
      disk.dsk[0x226] == 0 || disk.dsk[0x227] == 0) {
    retval = 3;
    fprintf( stderr, "Not a Flex disk image: ");

// Test OS/9
    long size = (((long)disk.dsk[0]*256)+(long)disk.dsk[1])*256 + (long)disk.dsk[2];
    if (size == nb_sectors) {
      fprintf( stderr, "Probably an OS-9 disk...\n");
    } else {
// Test Uniflex
      size = (((long)disk.dsk[0x212]*256) + (long)disk.dsk[0x213] +
      (long)disk.dsk[0x23F])*256 + (long)disk.dsk[0X214] + (long)disk.dsk[0X240] + 1;
      if (size * 2 == nb_sectors)
        fprintf( stderr, "Probably an UniFLEX disk...\n");

// Test FDOS. Simple enough, so display directory if -v
      else if (disk.size == 89600 && disk.dsk[0x1400] == '$' && disk.dsk[0x1401] == 'D'
                && disk.dsk[0x1402] == 'O' && disk.dsk[0x1403] == 'S') {
        if (verbose)
          fdosdir();
// If not verbose, just give some info on stderr
        else
          fprintf( stderr, "SWTPC 6800 FDOS disk (35 tracks of 10 sectors) detected.\n");

// It's something other...
      } else
        fprintf( stderr, "Unknown disk image type\n");
    }
    exit( retval);
  }

  disk.volnum = disk.dsk[0x21b]*256 + disk.dsk[0x21c];
// Size of disk & free sector list
  disk.nbtrk = disk.dsk[0x226];
  disk.nbsec = disk.dsk[0x227];
  disk.freesec = disk.dsk[0x221]*256 + disk.dsk[0x222];

// Too much free sectors for the disk ?
  if (disk.freesec > disk.nbtrk * disk.nbsec) {
    fprintf( stderr, "%sERROR: SIR Corrupted or not a Flex image%s\n", s_err, s_norm);
    exit( 3);
  }

// Print info about the disk
  if (!quiet) {
    printf( "Flex Volume name: %s, %d tracks, %d sectors/track\n",
      disk.label, disk.nbtrk+1, disk.nbsec);
    printf( "Flex Volume number: %d\n", disk.volnum);
    printf( "Creation Date: %d-%02d-%02d\n",
      disk.dsk[0x225]>50?disk.dsk[0x225]+1900:disk.dsk[0x225]+2000,
      (disk.dsk[0x223]&0x0f)+1, disk.dsk[0x224]);
    printf( "Highest Sector address on disk: %02x/%02x\n", disk.dsk[0x226], disk.dsk[0x227]);
  }

// Try to guess disk geometry
  if ((disk.nbtrk+1) * disk.nbsec == nb_sectors) {
    if (!quiet) {
      printf ( "Looks like a Single Density disk");
      if (disk.nbsec > 18)
        printf( " (but could be a DD disk with a DD track 0\n");
      else
        putchar( '\n');
    }
    disk.track0l = disk.nbsec;
  } else {
    disk.track0l = nb_sectors - disk.nbtrk * disk.nbsec;
    if ((disk.nbsec >= 36 && disk.track0l == 20) ||
      (disk.nbsec == 18 && disk.track0l == 10) ||
      (disk.track0l == disk.nbsec/2)) {
      if (!quiet)
        printf ( "Looks like a Double Density disk with Single Density track 0 of %d sectors\n",
          disk.track0l);
    } else if (disk.track0l > disk.nbsec) {
// Weird geometry... but can happen when disks are in EEPROM
      if (!quiet)
        printf( "%sUnknown geometry: %d tracks of %d sectors + first track of %d sectors !%s\n",
          s_warn, disk.nbtrk, disk.nbsec, disk.track0l, s_norm);
      disk.track0l = disk.nbsec;
      disk.nbtrk++;
      last_trk_sec = nb_sectors - (disk.nbtrk-1) * disk.nbsec - disk.track0l;
      if (!quiet)    
        printf( "%s  => Using normal %d sector track 0, add a %d%s incomplete track of %d sectors%s\n",
          s_warn, disk.track0l, disk.nbtrk, "th", last_trk_sec, s_norm);
    } else if (disk.track0l > disk.nbsec/2 && disk.track0l < disk.nbsec) {
      if(!quiet)
        printf ( "Looks like a Double Density disk with Single Density track 0 of %d sectors\n",
          disk.track0l);
    } else {
      disk.nbtrk -= (((disk.nbtrk * disk.nbsec - nb_sectors) / disk.nbsec) + 1);
// This is generaly no good
      if (!quiet) {
        printf( "%sERROR: Disk image too small... unusual geometry or truncated ?\n", s_err);
        printf( "%sReducing number of tracks to %d, %s", s_warn, disk.nbtrk, s_norm);
      }
      if (disk.nbsec < 25)
          disk.track0l = disk.nbsec;
      else
        disk.track0l = nb_sectors - disk.nbtrk * disk.nbsec;
// Try the highest probability value
      if (!quiet)
        printf( "%strying with first track of %d sectors%s\n", s_warn, disk.track0l, s_norm);
    }
  }

// Print general stats
  if (!quiet) {
    printf( "Number of data sectors: %d\n", disk.nbtrk * disk.nbsec);
    printf( "Free sectors: %d [%02x/%02x - %02x/%02x]\n", disk.freesec,
      disk.dsk[0x21d], disk.dsk[0x21e], disk.dsk[0x21f], disk.dsk[0x220]);
    if (disk.freesec == 0)
      printf( "%sWarning: empty free list%s\n", s_warn, s_norm);
  }

// table of all blocs of the disk:
// tabsec may contain :
// - the entry in directory from the file using it
// - 0 for a directory bloc
// - -1 for a free sector
// - -99999 for a bloc not reclaimed
  tabsec = malloc( sizeof(int) * nb_sectors);
  nxtsec = malloc( sizeof(int) * nb_sectors);
  for (k = 0; k < nb_sectors; k++) {
    tabsec[k] = -99999; // initialy not used
    nxtsec[k] = 0;
  }

// creating a table of freelist blocs
  cftrk = disk.dsk[0x21d];
  cfsec = disk.dsk[0x21e];

  for (k = 0; k < disk.freesec; k++) {
    if ((ibloc = ts2blk( cftrk, cfsec)) < 0) {
      if (!quiet)
        printf( "%sERROR: sector link out of bounds [0x%02x/0x%02x] in freelist%s\n",
          s_err, cftrk, cfsec, s_norm);
      retval = 1;
      break;
    }
    current_sector = ts2pos( cftrk, cfsec);
    if (tabsec[ibloc] == -99999)
      tabsec[ibloc] = -1;
    else
      tabsec[ibloc]--; // free bloc twice in list: must not happens
    cftrk = current_sector[0];
    cfsec = current_sector[1];
    nxtsec[ibloc] = ts2blk( cftrk, cfsec);

    if (cftrk == 0 && cfsec == 0)
        break;
  }
  for (j = 0; j < k+1; j++) {
    if (tabsec[j] < -1 && tabsec[j] != -99999) {
      if (!quiet)
        printf( "%sERROR sector [0x%02x/0x%02x] %d times in freelist%s\n",
          s_err, blk2trk( j), blk2sec( j), -tabsec[j], s_norm);
      retval = 1;
    }
  }
// to be confirmed later
  if (k != disk.freesec-1 && disk.freesec != 0) {
    retval = 1;
    if (!quiet)
      printf( "%sBad free sectors list length: chain of %d sectors instead of %d%s\n",
        s_warn, k+1, disk.freesec, s_norm);
  }

// Blocs in directory

  nfile = 1;
  nslot = 1;
  ndel = 0;
  usedsec = 0;
  dirsec = 0;
  
  ibloc = ts2blk( 0, 5);
  if (tabsec[ibloc] < -1)
    tabsec[ibloc] = 0; // value for directory bloc
  else {
    retval = 1;
	if (repar)
	  tabsec[ibloc] = 0;
    if (!quiet)
      printf( "%sERROR: directory sector 0(0x00)/5(0x05) also in freelist%s\n", s_err, s_norm);
  }

  base = ts2pos( 0, 5) ; // dir on sector 5 (offset = 0x400)

  while (nslot < DIRSIZE) {	// TODO should be dynamic
    if (base[0])            // directory bloc ouside track 0
      dirsec++;
    for (k=0; k < 10 && nslot < DIRSIZE; k++) {
        entry = base + 16 + (24 * k);
        if (*entry != 0) {
        file[nfile].flags = 0;
        if (getname( entry, name, 1) < 0) {
          printf( "%sERROR: Directory entry %d : name not valid%s\n",
            s_err, nfile, s_norm);
          file[nfile].flags |= 0x40;
        }
        j = ts2blk( entry[0xd], entry[0xe]);
        if ((j < 1 || j == 2) && *name != 0xFF && entry[0x12] != 0) { 

          printf( "%sERROR: Directory entry %d (%s) : sector [%02X,%02X] not valid%s\n",
            s_err, nfile, name, entry[0xd], entry[0xe], s_norm);
          file[nfile].length = 0;
          file[nfile].flags |= 0x80;
          retval = 2;
          continue;
        }

        dsktime.tm_hour = 12;
        dsktime.tm_min = 0;
        dsktime.tm_sec = 0;
        dsktime.tm_isdst = 0;
        dsktime.tm_mday = entry[0x16];
        if (entry[0x15] < 1 || entry[0x15] > 12)
            dsktime.tm_mon = 0;
        else
            dsktime.tm_mon = (int)entry[0x15] - 1;
        if (entry[0x17] > 75)
            dsktime.tm_year = (int)entry[0x17];
        else
            dsktime.tm_year = (int)entry[0x17] + 100;

        file[nfile].mtime = mktime( &dsktime);
        file[nfile].pos = entry;

        if (entry[0x13]) {
          file[nfile].flags |= 2;
        }
        if (entry[0] == 0xFF) {
          name[0] = '?';
          ndel++;
          file[nfile].flags |= 1;
        } else {
            usedsec += (entry[0x11]*256+entry[0x12]);
        }
          strcpy( file[nfile].name, name);
            file[nfile].start_trk = entry[0xd];
            file[nfile].start_sec = entry[0xe];
            file[nfile].end_trk   = entry[0xf];
            file[nfile].end_sec   = entry[0x10];
            file[nfile].length    = entry[0x11]*256+entry[0x12];
        if (file[nfile].length == 0) {
          ndel++;
          file[nfile].flags |= 1;
        }
            nfile++; nslot++;
        } else {
        nslot++;
        file[nslot].pos = entry;
        }
    }
    // Read next directory bloc
    if (base[0] == 0 && base[1] == 0)
      break;
    if ((ibloc = ts2blk( base[0], base[1])) <0 ) {
      if (!quiet)
        printf( "%sERROR: sector link out of bounds (0x%02x/0x%02x) in directory%s\n",
          s_err, base[0], base[1], s_norm);
      retval = 2;
      break;
    }
    if (tabsec[ibloc] < -1)
      tabsec[ibloc] = 0;
    else {
      if (tabsec[ibloc] == -1) {
        retval = 1;
		tabsec[ibloc] = 0;
        if (!quiet)
          printf( "%sERROR: Directory sector %d(0x%02x)/%d(0x%02x) also in freelist%s\n",
            s_err, base[0], base[0], base[1], base[1], s_norm);
      } else {
        retval = 2;
        if (!quiet)
          printf( "%sERROR: Directory sector %d(0x%02x)/%d(0x%02x) twice used (loop)%s\n",
            s_err, base[0], base[0], base[1], base[1], s_norm);
        break; // No need to go further !
      }
    }
    base = ts2pos( base[0], base[1]);
  }

// File linking analyse...
  nfile--;
  nslot--;

  int nb_blk;
  for( k=1; k <= nfile; k++) {
    cftrk = file[k].start_trk;
    cfsec = file[k].start_sec;
    nb_blk = 0;
    if (file[k].name[0] == 0 || (file[k].flags & 0x80) != 0) // Nothing to do with it
      continue;
    if (file[k].length == 0)
      continue;
    if (file[k].name[0] == '?' || (file[k].flags & 0x01) != 0) { // Deleted file
      file[k].flags |= 0x20;        // We hope it can be restored
      ibloc = ts2blk( cftrk, cfsec);
      for (j = 0; j < file[k].length; j++) {
        if (ibloc < 0) {            // nope
          file[k].flags &= 0xdf;
          break;
        }
        if (tabsec[ibloc] >= 0) {    // nope
          file[k].flags &= 0xdf;
          break;
        }
        if (j == file[k].length - 1 && ibloc != ts2blk( file[k].end_trk, file[k].end_sec))
          file[k].flags &= 0xdf;    //nope
        ibloc = nxtsec[ibloc];
      }
    continue;
    }

    while (cftrk != 0 || cfsec != 0) {    // Normal file
      if (ts2blk( cftrk, cfsec) < 0) {
        retval = 2;
        if (!quiet)
          printf( "%sERROR: sector (0x%02x/0x%02x) out of bounds for file %s (%d)%s\n",
            s_err, cftrk, cfsec, file[k].name, k, s_norm);
        break;
      }
      ibloc = tabsec[ts2blk( cftrk, cfsec)];
      current_sector = ts2pos( cftrk, cfsec);
      nxtsec[ts2blk( cftrk, cfsec)] = ts2blk(current_sector[0], current_sector[1]);
      nb_blk++;
      if (ibloc < -1)
        tabsec[ts2blk( cftrk, cfsec)] = k;
      else if (ibloc == -1) {
        printf( "%sERROR: File %s (%d), sector %d(0x%02x)/%d(0x%02x) also in freelist%s\n",
          s_err, file[k].name, k, cftrk, cftrk, cfsec, cfsec, s_norm);
        file[k].flags |= 0x40;
        if (retval < 1)
          retval = 1;
      } else if (ibloc == 0) {
        printf( "%sERROR: File %s (%d), sector %d(0x%02x)/%d(0x%02x) also in directory%s\n",
          s_err, file[k].name, k, cftrk, cftrk, cfsec, cfsec, s_norm);
        file[k].flags |= 0x80;
        retval = 2;
      } else {
        printf( "%sERROR: File %s (%d), sector %d(0x%02x)/%d(0x%02x) also in file %s (%d)%s\n",
          s_err, file[k].name, k, cftrk, cftrk, cfsec, cfsec, file[ibloc].name, ibloc, s_norm);
        file[k].flags |= 0x80;
		if (k == ibloc)
		  break;
        file[ibloc].flags |= 0x80;
      }
      cftrk = current_sector[0];
      cfsec = current_sector[1];
    }
    if (nb_blk != file[k].length) {
      printf( "%sERROR: lenght of %s %d, but %d sectors chained%s\n",
        s_err, file[k].name, file[k].length, nb_blk, s_norm);
      file[k].flags |= 0x40;
    }
  }

// General statistics...
  if (!quiet) {
    printf( "Total number of sectors used by files: %d\n", usedsec);
    printf( "Number of sectors used by directory outside track 0: %d\n", dirsec);
  }

// Print list of files...
  if (nfile > 0) {
    if (verbose) {
      printf( "\nFile list (%d used + %d deleted / %d entries):\n", nfile - ndel, ndel, nslot);
      printf( " id       Filename    start    end    size      date    flags\n");
      for( k=1; k <= nfile; k++) {
        printf( "%3d %14s [%02x/%02x - %02x/%02x] %5d",
          k, file[k].name, file[k].start_trk, file[k].start_sec,
          file[k].end_trk, file[k].end_sec, file[k].length);
        ftime = localtime( &file[k].mtime);
        printf( "   %d-%02d-%02d", ftime->tm_year+1900, ftime->tm_mon+1, ftime->tm_mday);
        if (file[k].flags & 2)
            printf( " random access");
        if (file[k].flags & 1)
          printf( " DELETED");
        if (file[k].flags & 0x80)
          printf( " %sCORRUPTED%s", s_err, s_norm);
        if (file[k].flags & 0x40)
          printf( " %sUNUSABLE%s", s_warn, s_norm);
        if (file[k].flags & 0x20)
          printf( " (maybe recoverable)");
        putchar( '\n');
      }
    } else if (!quiet) {
      printf( "\nFile list (%d/%d):\n", nfile, nslot);
      for( int j=0, k=1; k <= nfile; k++)
        if ((file[k].flags && 0x81) == 0) {
          printf( "%12s     ", file[k].name);
          if ((++j % 5) == 0)
            putchar( '\n');
        }
      putchar( '\n');
    }
  } else if (!quiet)
    printf( "Empty directory (%d entries).\n", nslot);

// Reserved sectors verification

  for (k=0; k < 4; k++) {
    if (tabsec[k] == -1) {
      if (retval < 1)
        retval = 1;
      if (!quiet)
        printf( "%sWarning: reserved sector [00/%02x] in freelist%s\n", s_warn, k+1, s_norm);
    } else if (tabsec[k] == 0) {
      if (retval < 1)
        retval = 1;
      if (!quiet)
        printf( "%sWarning: reserved sector [00/%02x] in directory space%s\n",
          s_warn, k+1, s_norm);
    } else if (tabsec[k] > 0) {
      if (retval < 1)
        retval = 1;
      if (!quiet)
        printf( "%sWarning: reserved sector [00/%02x] in file %s (%d)%s\n",
          s_warn, k+1, file[tabsec[k]].name, tabsec[k], s_norm);
    }
  }
  int notused = 0;
  for (k=5; k < nb_sectors; k++) {
    if (tabsec[k] == -99999) {
      notused++;
    }
  }
  if (notused) {
    if (retval < 1)
      retval = 1;
    if (!quiet) {
      printf( "%sWarning : %d sector(s) missing in freelist%s\n", s_warn, notused, s_norm);
      for (k=5; k < nb_sectors; k++)
        if (tabsec[k] == -99999)
          if (verbose)
            printf( "[%02x/%02x]   ", blk2trk( k), blk2sec( k));
      putchar( '\n');
    }
  }
  if (!repar || retval > 1)
    return retval;
// Build a new freelist if needed...
  int newfreesec = notused + disk.freesec;
  if (notused) {
    for (k=5; k < nb_sectors; k++)
      if (tabsec[k] < -1) {
        current_sector = disk.dsk + k * SECSIZE;
        nxtsec[k] = ts2blk( current_sector[0], current_sector[1]);
        tabsec[k] = -1;
      }
  }
  if (verbose) {
    if (repar)
        printf( "Updated list:\n");
    else
        printf( "%sFree sector list must be corrected (run with -r option) :%s\n", s_warn, s_norm);
    for (k=5; k < nb_sectors; k++)
      if (tabsec[k] == -1)
        printf( "%d [%02x/%02x] --> %d\n", k, blk2trk( k), blk2sec( k), nxtsec[k]);
  }
  if (!repar)
    return retval;

// Reorganise or create new free sector list
  free_nb = 0;
  reorg = 0;
  free_start = ts2blk( disk.dsk[0x21d], disk.dsk[0x21e]);
  k = 5;
  while (k < nb_sectors) {
    if (tabsec[k] != -1) {
      k++;
      continue;
    }
    if (free_nb == 0 && free_start != k) {
      disk.dsk[0x21d] = blk2trk( k);
      disk.dsk[0x21e] = blk2sec( k);
      reorg++ ;
    }
    free_nb++;
    j = k + 1;
    while( j < nb_sectors && tabsec[j] != -1)
      j++;
    current_sector = disk.dsk + k * SECSIZE;
    if (j == nb_sectors) {
      if (current_sector[0] != 0 || current_sector[1] != 0) {
        current_sector[0] = 0;
        current_sector[1] = 0;
        reorg++;
      }
      break;
    }
    if (nxtsec[k] != j) {
      current_sector[0] = blk2trk(j);
      current_sector[1] = blk2sec(j);
      reorg++;
    }
    k = j;
  }
  if (disk.freesec != free_nb) {
    disk.dsk[0x221] = (uint8_t)((free_nb/256) & 0xFF); 
    disk.dsk[0x222] = (uint8_t)((free_nb%256) & 0xFF);
    reorg++;
  }

// Recovery of deleted entries space in directory
// Since the linked list of free sectors was modified
// They can be restored any more, so we compact the directory
  k = ndel;
  for (i = 1; i <= nfile; i++) {
    while ((file[nfile].flags &= 1) != 0) {
      dir = file[nfile].pos;
      for (j = 0; j < 24; j++)
        dir[j] = 0;
      nfile--;
    }
    if ((file[i].flags &= 1) != 0) {
      entry = file[i].pos;
      dir = file[nfile].pos;
      for (j = 0; j < 24; j++) {
        entry[j] = dir[j];
        dir[j] = 0;
      }
      nfile--;
      if (--k == 0)
        break;
    }
  }

// Final stats printed

  if (!quiet) {
    if (ndel)
      printf( "Recovery of %d entries deleted, now %d/%d entries in directory.\n",
        ndel, nfile, nslot);
    if (reorg)
      printf( "New free list of %d sectors created (%d modifications)\n", free_nb, reorg);
    else
      printf( "Freelist clean: no modification needed\n");
  }
  if (reorg || ndel)
    if (msync( disk.dsk, disk.size, MS_SYNC) < 0) {
      perror( "Freelist update failed");
      return 1;
    }
  return munmap( disk.dsk, disk.size);
}

// Program start here

int main( int argc, char **argv)
{
  int opt;
  char *filename;
  struct stat dsk_stat;
  int flags;
  char *term, *getenv( const char *name);

// Read parameters
  while ((opt = getopt( argc, argv, "hvqr")) != -1) {
    switch (opt) {
    case 'h':
      usage( *argv);
      exit( 0);
      break;
    case 'v':
      verbose = 1;
      break;
    case 'q':
      quiet = 1;
      break;
    case 'r':
      repar = 1;
      break;
    default: /* '?' */
      usage( *argv);
      exit( 3);
    }
  }

// Some sanitary checking on options

  if (quiet && verbose) {
    fprintf( stderr, "Options '-q' and '-v' are exclusive, '-q' ignored\n");
    quiet = 0;
  }

  if (optind < argc) {
    filename = argv[ optind++];
    if (optind < argc) {
      fprintf( stderr, "Only one filname is allowed\n");
      usage( *argv);
      exit (3);
    }
  } else {
    fprintf( stderr, "No file name ???\n");
    usage( *argv);
    exit( 3);
  }

// More checking on file
  if (stat( filename, &dsk_stat)) {
    perror( filename); 
    exit( 3);
  }

// If possible, colorize Warnings and errors
  if ((term = getenv( "TERM")) != NULL) {
    if (strstr( term, "color") != NULL) {
      s_warn = "\e[1;93m";
      s_err  = "\e[1;91m";
      s_norm = "\e[0m";
    } else {
      s_err = s_warn = s_norm = "";
    }
  }

// Open disk image and map it in memory
  strncpy( disk.filename, filename, 256);
  disk.shortname = strrchr( disk.filename, '/');
  if (disk.shortname == NULL)
    disk.shortname = disk.filename;
  else
    disk.shortname++;

  disk.size = dsk_stat.st_size;
  if ((dsk_stat.st_mode & S_IWUSR) && repar) {
    disk.readonly = 0;
    disk.fd = open( disk.filename, O_RDWR);
    flags = PROT_READ | PROT_WRITE;
    disk.dsk = mmap( &disk.dsk, dsk_stat.st_size, flags, MAP_SHARED, disk.fd, 0);
  } else {
    disk.readonly = 1;
    disk.fd = open( disk.filename, O_RDONLY);
    flags = PROT_READ;
    disk.dsk = mmap( &disk.dsk, dsk_stat.st_size, flags, MAP_PRIVATE, disk.fd, 0);
    if (repar) {
      repar = 0;
      fprintf( stderr, "Warning: file %s is READ_ONLY, option '-r' ignored\n", filename);
    }
  }


  if (disk.dsk == NULL) {
    perror( filename);
    exit( 3);
  }

  return browse_dsk();
}
