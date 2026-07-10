/* vim:ts=4
 * flan.c -- Flex FLoppy ANalyser 
   Copyright (C) 2022-2026 Michel Wurtz - mjwurtz@gmail.com

   Utility to list, analyse, and repar FLEX floppy images

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

int verbose = 0; // more details when verbose increase
int quiet = 0;   // don't print unnecessary messages

char *s_err,     // If color is supported => errmsg in red
     *s_warn,    // warnings in yellow
     *s_norm;    // return to normal

// Help message
void usage( char *cmd) {
  fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
  fprintf( stderr, "       %s [-q|-v] [-r] <file>\n", cmd);
  fprintf( stderr, "Options:\n");
  fprintf( stderr, "   -q => quiet, don't print anything\n");
  fprintf( stderr, "   -r => repair and/or reorder free sector list\n");
  fprintf( stderr, "   -v => print more details\n");
}

// Analyse the content of the disk loaded

int browse_dsk( int retval) {

  int j, k;
 
// General statistics...
  if (!quiet)
    printf( "Total number of sectors used by files: %d\n", usedsec);

// Print list of files...
  if (!quiet)
	list_files( verbose);

// Reserved sectors verification

  for (k=0; k < 4; k++) {
    if (tabsec[k] == -1) {
      if (retval < 1)
        retval = 1;
      if (!quiet)
        printf( "%sWarning: reserved sector [00/%02X] in freelist%s\n", s_warn, k+1, s_norm);
    } else if (tabsec[k] == 0) {
      if (retval < 1)
        retval = 1;
      if (!quiet)
        printf( "%sWarning: reserved sector [00/%02X] in directory space%s\n",
          s_warn, k+1, s_norm);
    } else if (tabsec[k] > 0) {
      if (retval < 1)
        retval = 1;
      if (!quiet)
        printf( "%sWarning: reserved sector [00/%02X] in file %s (%d)%s\n",
          s_warn, k+1, file[tabsec[k]].name, tabsec[k], s_norm);
    }
  }
  return retval;
}

// Rebuild the free list and compact directory if needed
//
int repar_dsk( int repar) {
  int i, j, k;
  int ibloc, obloc;
  uint8_t *current_sector;
  uint8_t *orig, *dest;

  int free_nb, reorg, free_start; // For free list reorganisation

// Verify real size of disk... correct if false
  if (disk.nbtrk < disk.dsk[0x226]) {
	disk.dsk[0x226] = disk.nbtrk;
	reorg++;
  }

// Build a new freelist if needed...
  if (notused)
    for (ibloc = disk.track0l; ibloc < disk.nb_sectors; ibloc++)
      if (tabsec[ibloc] < -1)
        tabsec[ibloc] = -1;

// Reorganise or create new free sector list
  free_nb = 0;
  reorg = 0;
  free_start = ts2blk( disk.dsk[0x21d], disk.dsk[0x21e]);
// Find first sector
  ibloc = disk.track0l;
  while (ibloc < disk.nb_sectors)
    if (tabsec[ibloc] == -1)
	  break;
    else
      ibloc++;
  if (ibloc == disk.nb_sectors) { // Empty free list
	disk.dsk[0x21d] = 0;
	disk.dsk[0x21e] = 0;
	disk.dsk[0x21f] = 0;
    disk.dsk[0x220] = 0;
  } else {
	if (free_start != ibloc) {
      disk.dsk[0x21d] = blk2trk( ibloc);
      disk.dsk[0x21e] = blk2sec( ibloc);
      reorg++ ;
	}
	free_nb++;
	obloc = ibloc;
	ibloc++;
    while (ibloc < disk.nb_sectors) {
	  if (tabsec[ibloc] != -1) {
		ibloc++;
		continue;
	  }
	  free_nb++;
	  if (nxtsec[obloc] != ibloc) {
		disk.dsk[obloc * SECSIZE] = blk2trk( ibloc);
		disk.dsk[obloc * SECSIZE + 1] = blk2sec( ibloc);
		reorg++;
	  }
	  obloc = ibloc;
      ibloc++;
    }

    if (nxtsec[obloc] != 0) {
	  disk.dsk[obloc * SECSIZE] = 0;
	  disk.dsk[obloc * SECSIZE + 1] = 0;
	  reorg++;
	}
	if (obloc != ts2blk( disk.dsk[0x21f], disk.dsk[0x220])) {
	  disk.dsk[0x21f] = blk2trk( obloc);
      disk.dsk[0x220] = blk2sec( obloc);
	  reorg++;
	}
  }

  if (disk.freesec != free_nb) {
    disk.dsk[0x221] = (uint8_t)((free_nb/256) & 0xFF); 
    disk.dsk[0x222] = (uint8_t)((free_nb%256) & 0xFF);
    reorg++;
  }

//////////////////////////////////////////////////////
// Recovery of deleted entries space in directory   //
// If the linked list of free sectors was modified, //
// the deleted files can't be restored any more     //
//////////////////////////////////////////////////////

  if ((ndel && reorg) || repar > 1) {
    j = 0;
	nfile = nslot-1;
	while (nfile >=0 && file[nfile].flags == 0)
	  nfile--;                       // go before last free slot
	while ((file[j].flags & 0x10) != 0x10)
	  j++;                           // go to next deleted file
	while (j < nfile) {
	  if ((file[nfile].flags & 0x10) != 0x10) {
	    orig = file[nfile].pos;
		dest = file[j].pos;
		for (i = 0; i < 24; i++)     // move last entry 
		  dest[i] = orig[i];
		j++;
		file[j].flags = 1;
	  }
	  orig = file[nfile].pos;
      for (i = 0; i < 24; i++)       // clean last deleted entry
		orig[i] = 0;
	  file[nfile].flags = 0;
	  nfile--;
	  while (j < nfile && (file[j].flags & 0x10) != 0x10)
		j++;
	}
	if ((file[nfile].flags & 0x10) == 0x10) {
	  dest = file[nfile].pos;
	  for (i = 0; i < 24; i++)       // clean last deleted entry
		dest[i] = 0;
	  file[nfile].flags = 0;
	}
  }

// Final stats printed

  if (!quiet) {
    if ((ndel && reorg) || repar > 1)
      printf( "Recovery of %d entries deleted, now %d/%d entries in directory.\n",
          ndel, nfile, nslot);
	if (reorg)
      printf( "New free list of %d sectors created (%d modifications)\n", free_nb, reorg);
	else
      printf( "Freelist clean: no modification needed\n");
  }

  return 0;
}

// Program start here

int main( int argc, char **argv)
{
  int opt;
  char *filepath;
  char *backup;
  struct stat dsk_stat;
  char *term, *getenv( const char *name);
  int repar = 0; // image must be repared and/or freelist reorganised

  int retval;    // value returned if problem detected
                 // 0: everything is ok ; 1: freelist uncomplete or damaged
                 // 2: disk structure not reparable ; 3: unable to process 
// Read parameters
  while ((opt = getopt( argc, argv, "hvqr")) != -1) {
    switch (opt) {
    case 'h':
      usage( *argv);
      exit( 0);
      break;
    case 'v':
      verbose++;
      break;
    case 'q':
      quiet = 1;
      break;
    case 'r':
      repar++;
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
    filepath = argv[ optind++];
    if (optind < argc) {
      fprintf( stderr, "Only one filename is allowed\n");
      usage( *argv);
      exit (3);
    }
  } else {
    fprintf( stderr, "No file name ???\n");
    usage( *argv);
    exit( 3);
  }

// More checking on file
  if (stat( filepath, &dsk_stat)) {
    perror( filepath); 
    exit( 3);
  }

// If possible, colorize Warnings and Errors
  if (isatty( 1) && (term = getenv( "TERM")) != NULL) {
    if (strstr( term, "256color") != NULL) {
      s_warn = "\e[1;93m";
      s_err  = "\e[1;91m";
      s_norm = "\e[0m";
    } else {
      s_err = s_warn = s_norm = "";
    }
  }

// Open disk image and map it in memory
  disk.shortname = strrchr( filepath, '/');
  if (disk.shortname == NULL)
    disk.shortname = filepath;
  else
    disk.shortname++;

  if ((dsk_stat.st_mode & S_IWUSR) && repar) {
    disk.readonly = 0;
  } else {
    disk.readonly = 1;
    if (repar) {
      repar = 0;
      printf( "%sWarning: file %s is READ_ONLY, option '-r' ignored%s\n",
        s_warn, filepath, s_norm);
    }
  }

  if ((disk.fd = open( filepath, O_RDONLY)) < 0 ) {
    perror( filepath);
    exit( 3);
  }

  disk.size = dsk_stat.st_size;
  disk.dsk = malloc( dsk_stat.st_size);

  if (disk.dsk == NULL) {
    perror( "malloc: ");
    exit( 3);
  }

  if (read( disk.fd, disk.dsk, dsk_stat.st_size) != dsk_stat.st_size) {
    perror( filepath) ;
    exit( 3);
  }
  close( disk.fd);

  if (! isFlex( disk.dsk, disk.nb_sectors))
    exit( 2);

  if ((badFlex( 0) & 0xFF) > 1)
    exit( 2);

  retval = analyse(!quiet);
  if (!repar && retval > 1)
    return retval;

  retval = browse_dsk(retval);

  if (!repar)
    return retval;

  if (retval = repar_dsk( repar)) {
    printf( "%sUnable to restore consistency, aborting.%s\n", s_err, s_norm);
    return retval;
  }

  backup = malloc( strlen( filepath) + 4);
  strcpy( backup, filepath);
  strcat( backup, ".bak");
  if (rename( filepath, backup)) {
    perror( backup);
    return 3;
  }
  disk.fd = creat( filepath, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

  if (disk.fd == -1) {
    perror( "create: ");
    exit( 3);
  }

  if (write( disk.fd, disk.dsk, dsk_stat.st_size) != dsk_stat.st_size) {
    perror( "rewrite:") ;
    exit( 3);
  }
  close( disk.fd);

}
