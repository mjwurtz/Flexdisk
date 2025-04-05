/* fldump.c -- Flex floppy extractor
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

static int retval = 0;
static int verbose = 0;
static int quiet = 0;
static int all = 0;
static int where = 0;

void usage( char *cmd) {
	fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
	fprintf( stderr, "       %s [-q|-v] [-b] [-a] <file>\n", cmd);
    fprintf( stderr, "Options:\n");
	fprintf( stderr, "   -a => extract deleted files if possible\n");
	fprintf( stderr, "   -b => directory for extracted files based on file name\n");
	fprintf( stderr, "   -q => quiet, don't print anything except error messages\n");
	fprintf( stderr, "   -v => print a detailled listing of files\n");
}

// Download file (text not converted, raw binary, random file tagged)

void download( int index, char *dir) {
  uint8_t *current, *next, cftrk, cfsec;
  FILE *out;
  int nb_blk, j;
  char path[32];
  char filename[20];
  struct utimbuf new_times;

  if (file[index].name[0] == '?' )
	if ((file[index].flags & 0x20) == 0 || all == 0)
	  return;
  if ((file[index].flags & 0x80) != 0)
	return;

  strcpy( path, dir);
  strcat( path, "/");
  if (file[index].name[0] == '?') {
	sprintf( filename, "_%d_%s", index, file[index].name+1);
	strcat( path, filename);
  } else
	strcat( path, file[index].name);
// Flag b added for DOS/Window systems
  if ((out = fopen( path, "wb")) == NULL)
	perror( path);
  cftrk = file[index].start_trk;
  cfsec = file[index].start_sec;
  current = ts2pos( cftrk, cfsec);
  if (file[index].flags & 0x02) {
	fputs( "#FLEX##RAND#", out);
	j = 16;
  } else
	j = 4;

  nb_blk = 0;
  while (nb_blk < file[index].length) {
	if (ts2blk( cftrk, cfsec) < 1) {
	  break;
	}
	current = ts2pos( cftrk, cfsec);
	nb_blk++;
	cftrk = current[0];
	cfsec = current[1];
	while (j < 256)
		fputc( current[j++], out);
	j = 4;
  }
  fclose( out);
  new_times.actime = time( NULL);
  new_times.modtime = file[index].mtime;
  utime( path, &new_times);

  if (cftrk != 0 || cfsec != 0 || nb_blk != file[index].length)
	printf( "Warning! file '%s' may be truncated...\n", path);
}

// Analyse the content and extract the files from the disk loaded

int browse_dsk() {

  uint8_t *current_sector, *dsk, *dir, *base, *entry, cftrk, cfsec;
  int sector_size = SECSIZE;
  int nb_sectors;
  int dirsec;	// Number of directory sectors outside track 0
  int ibloc, freediff;
  int j, k;
  char name[16], dirname[32];
  int errflag;
  struct tm dsktime, *ftime;
  uint8_t last_trk_sec;

  nb_sectors = disk.size / SECSIZE;
  if (!quiet) {
	printf( "\nFile name: %s\n", disk.shortname);
	printf( "Physical number of sectors: %u\n", disk.size / SECSIZE);
	if (nb_sectors * SECSIZE != disk.size)
	  printf( "[disk size don't match an integer number of sectors: %u bytes left]\n",
		disk.size % SECSIZE);
  }

// Not a flex disk ?
  if (getname( disk.dsk + 0x210, disk.label, 0) < 0 || disk.dsk[0x226] == 0 || disk.dsk[0x227] == 0) {
	retval = 3;
	// Test OS/9
		fprintf( stderr, "Not a Flex disk image: ");
	long size = (((long)disk.dsk[0]*256)+(long)disk.dsk[1])*256 + (long)disk.dsk[2];
	if (size == nb_sectors) {
		fprintf( stderr, "Probably an OS-9 disk...\n");
	} else {
		size = (((long)disk.dsk[0x212]*256)+(long)disk.dsk[0x213]+(long)disk.dsk[0x23F])*256 + (long)disk.dsk[0X214] + (long)disk.dsk[0X240] + 1;
// Test Uniflex
		if (size * 2 == nb_sectors)
			fprintf( stderr, "Probably an UniFLEX disk...\n");
// Test FDOS
		else if (disk.size == 89600 && disk.dsk[0x1400] == '$' && disk.dsk[0x1401] == 'D'
				&& disk.dsk[0x1402] == 'O' && disk.dsk[0x1403] == 'S')
			  fprintf( stderr, "SWTPC 6800 FDOS disk (35 tracks of 10 sectors) detected.\n");
// It's something other
		else
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
  if (disk.freesec > disk.nbtrk * disk.nbsec && !quiet) {
    printf( "Warning: Number of free sectors bigger than disk size\n");
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
	  if (!quiet)
		printf( "Unknown geometry: %d tracks of %d sectors + first track of %d sectors !\n",
		  disk.nbtrk, disk.nbsec, disk.track0l);
	  disk.track0l = disk.nbsec;
	  disk.nbtrk++;
	  last_trk_sec = nb_sectors - (disk.nbtrk-1) * disk.nbsec - disk.track0l;
	  if (!quiet)	
		printf( "  => Using normal %d sector track 0, add a %d%s incomplete track of %d sectors\n",
		  disk.track0l, disk.nbtrk, "th", last_trk_sec);
	} else if (disk.track0l > disk.nbsec/2 && disk.track0l < disk.nbsec) {
	  if(!quiet)
		printf ( "Looks like a Double Density disk with Single Density track 0 of %d sectors\n",
		  disk.track0l);
	} else {
	  disk.nbtrk -= (((disk.nbtrk * disk.nbsec - nb_sectors) / disk.nbsec) + 1);
	  if (!quiet) {
		printf( "ERROR: Disk image too small... unusual geometry or truncated ?\n");
		printf( "Reducing number of tracks to %d, ", disk.nbtrk);
	  }
	  if (disk.nbsec < 25)
	  	disk.track0l = disk.nbsec;
	  else
		disk.track0l = nb_sectors - disk.nbtrk * disk.nbsec;
	  if (!quiet)
		printf( "trying with first track of %d sectors\n", disk.track0l);
	}
  }

// table of all blocs of the disk:
  tabsec = malloc( sizeof(int) * nb_sectors);
  nxtsec = malloc( sizeof(int) * nb_sectors);
  for (k = 0; k < nb_sectors; k++) {
	tabsec[k] = -99999; // initialy not used
	nxtsec[k] = 0;
  }

// table of freelist blocs
  cftrk = disk.dsk[0x21d];
  cfsec = disk.dsk[0x21e];

  for (k = 0; k < disk.freesec; k++) {
	if ((ibloc = ts2blk( cftrk, cfsec)) < 0) {
	  if (!quiet)
		printf( "ERROR: sector link out of bounds [0x%02x/0x%02x] in freelist\n", cftrk, cfsec);
	  retval = 1;
	  break;
	}
	current_sector = ts2pos( cftrk, cfsec);
	if (tabsec[ibloc] == -99999)
	  tabsec[ibloc] = -1;
	else
	  tabsec[ibloc]--; // nb of times used in freelist
	cftrk = current_sector[0];
	cfsec = current_sector[1];
	nxtsec[ibloc] = ts2blk( cftrk, cfsec);

	if (cftrk == 0 && cfsec == 0)
		break;
  }
  for (j = 0; j < k+1; j++) {
	if (tabsec[j] < -1 && tabsec[j] != -99999) {
	  if (!quiet)
		printf( "ERROR sector [0x%02x/0x%02x] %d times in freelist\n",
		  blk2trk( j), blk2sec( j), -tabsec[j]);
	  retval = 1;
	}
  }
// to be confirmed later
  if (k != disk.freesec-1) {
	retval = 1;
	if (!quiet)
	  printf( "Bad free sectors list length: chain of %d sectors instead of %d\n",
		k+1, disk.freesec);
  }

  nfile = 1;
  usedsec = 0;
  dirsec = 0;
  errflag = 0;
  
  ibloc = ts2blk( 0, 5);
  if (tabsec[ibloc] < -1)
	tabsec[ibloc] = 0; // value for directory bloc
  else {
	retval = 2;
	printf( "ERROR: directory sector 0(0x00)/5(0x05) also in freelist\n");
  }

  base = ts2pos( 0, 5) ; // dir on sector 5 (offset = 0x400)

  while (nfile < DIRSIZE) {  // TODO why limit this ? to be modified
	if (base[0])
	  dirsec++;
	for (k=0; k < 10 && nfile < DIRSIZE; k++) {
  	  entry = base + 16 + (24 * k);
  	  if (*entry != 0) {
		file[nfile].flags = 0;
	    if (getname( entry, name, 1) < 0) {
		  if (!quiet)
			printf( "ERROR: Directory entry %d with bad filename\n", nfile);
		  file[nfile].flags |= 0x40;
		}
		j = ts2blk( entry[0xd], entry[0xe]);
		if ((j < 1 || j == 2) && *name != 0xFF && entry[0x12] != 0) { 

		  printf( "ERROR: Directory entry %d (%s) : sector [%02X,%02X] not valid\n",
			nfile, name, entry[0xd], entry[0xe]);
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

	    if (entry[0x13]) {
		  file[nfile].flags |= 2;
		}
		if (entry[0] == 0xFF) {
		  name[0] = '?';
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
  	  	nfile++;
  	  }
	}
    // Read next directory bloc
	if (base[0] == 0 && base[1] == 0)
	  break;
	if ((ibloc = ts2blk( base[0], base[1])) <0 ) {
	  printf( "ERROR: sector link out of bounds (0x%02x/0x%02x) in directory\n",
		base[0], base[1]);
	  retval = 2;
	  exit( 2);
	}
	if (tabsec[ibloc] < -1)
      tabsec[ibloc] = 0;
	else {
	  if (tabsec[ibloc] == -1) {
		printf( "ERROR: Directory sector %d(0x%02x)/%d(0x%02x) also in freelist\n",
		  base[0], base[0], base[1], base[1]);
	  } else {
		printf( "ERROR: Directory sector %d(0x%02x)/%d(0x%02x) twice used (loop)\n",
		  base[0], base[0], base[1], base[1]);
	  }
	  exit( 2);
    }
    base = ts2pos( base[0], base[1]);
  }

// File linking analyse...
  nfile--;

  int nb_blk;
  for( k=1; k <= nfile; k++) {
	cftrk = file[k].start_trk;
	cfsec = file[k].start_sec;
    nb_blk = 0;
	if (file[k].name[0] == 0 || (file[k].flags & 0x80) != 0) // Nothing to do with it
	  continue;

	if (file[k].name[0] == '?' || (file[k].flags & 0x01) != 0) { // Deleted file
	  file[k].flags |= 0x20;		// We hope it can be restored
	  ibloc = ts2blk( cftrk, cfsec);
	  for (j = 0; j < file[k].length; j++) {
		if (ibloc < 0) {			// nope
		  file[k].flags &= 0xdf;
		  break;
		}
		if (tabsec[ibloc] >= 0) {	// nope
		  file[k].flags &= 0xdf;
		  break;
		}
		if (j == file[k].length - 1 && ibloc != ts2blk( file[k].end_trk, file[k].end_sec))
		  file[k].flags &= 0xdf;	//nope
		ibloc = nxtsec[ibloc];
	  }
	continue;
	}

	while (cftrk != 0 || cfsec != 0) {	// Normal file
	  if (ts2blk( cftrk, cfsec) < 0) {
		retval = 2;
		if (!quiet)
		  printf( "ERROR: sector (0x%02x/0x%02x) out of bounds for file %s (%d)\n",
			cftrk, cfsec, file[k].name, k);
		break;
	  }
	  ibloc = tabsec[ts2blk( cftrk, cfsec)];
	  current_sector = ts2pos( cftrk, cfsec);
	  nxtsec[ts2blk( cftrk, cfsec)] = ts2blk(current_sector[0], current_sector[1]);
	  nb_blk++;
	  if (ibloc < -1)
	    tabsec[ts2blk( cftrk, cfsec)] = k;
	  else if (ibloc == -1) {
	    printf( "ERROR: File %s (%d), sector %d(0x%02x)/%d(0x%02x) also in freelist\n",
		  file[k].name, k, cftrk, cftrk, cfsec, cfsec);
		file[k].flags |= 0x40;
		if (retval < 1)
		  retval = 1;
	  } else if (ibloc == 0) {
	    printf( "ERROR: File %s (%d), sector %d(0x%02x)/%d(0x%02x) also in directory\n",
		  file[k].name, k, cftrk, cftrk, cfsec, cfsec);
		file[k].flags |= 0x80;
		retval = 2;
	  } else {
		printf( "ERROR: File %s (%d), sector %d(0x%02x)/%d(0x%02x) also in file %s (%d)\n",
		  file[k].name, k, cftrk, cftrk, cfsec, cfsec, file[ibloc].name, ibloc);
		file[k].flags |= 0x80;
		if (k == ibloc)
		  break;
		file[ibloc].flags |= 0x80;
	  }
	  cftrk = current_sector[0];
	  cfsec = current_sector[1];
	}
	if (nb_blk != file[k].length) {
	  printf( "ERROR: lenght of %s %d, but %d sectors chained\n",
		file[k].name, file[k].length, nb_blk);
	  file[k].flags |= 0x40;
	}
  }

// General statistics...
  if (!quiet) {
	printf( "Total number of sectors used by files: %d\n", usedsec);
	printf( "Number of sectors used by directory outside track 0: %d\n", dirsec);
  }

// Print list of files...
  if (verbose) {
	printf( "\n id     Filename    start    end   size    date\n");
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
		printf( " CORRUPTED");
	  if (file[k].flags & 0x40)
		printf( " MAYBE DAMAGED");
	  if (file[k].flags & 0x20)
		printf( " (maybe recoverable)");
	  putchar( '\n');
	}
  } else if (!quiet) {
	printf( "\nFile list :\n");
	for( int j=0, k=1; k <= nfile; k++) {
	  if ((file[k].flags & 0x80) != 0)
		continue;
	  if ((file[k].flags & 0x21) == 1)
		continue;
	  if ((file[k].flags & 0x21) == 0x21 && all == 0)
		continue;
	  printf( "   %12s", file[k].name);
	  if ((++j % 5) == 0)
		putchar( '\n');
	}
	putchar( '\n');
  }

// Reserved sectors verification

  for (k=0; k < 4; k++) {
	if (tabsec[k] == -1) {
	  if (retval < 1)
		retval = 1;
	  if (verbose)
		printf( "Warning: reserved sector [00/%02x] in freelist\n", k+1);
	} else if (tabsec[k] == 0) {
	  if (retval < 1)
		retval = 1;
	  if (verbose)
		printf( "Warning: reserved sector [00/%02x] in directory space\n",
		  k+1);
	} else if (tabsec[k] > 0) {
	  if (retval < 1)
		retval = 1;
	  if (verbose)
		printf( "Warning: reserved sector [00/%02x] in file %s (%d)\n",
		  k+1, file[tabsec[k]].name, tabsec[k]);
	}
  }
  int notused = 0;
  for (k=5; k < nb_sectors; k++) {
	if (tabsec[k] == -99999) {
	  notused++;
	}
  }
  if (notused && verbose)
	printf( "Warning : %d sector(s) missing in freelist\n", notused);

// create a directory named after the volume name
  if (disk.label[0] && where == 0)
	sprintf( dirname, "%s_%u", disk.label, disk.volnum);
  else
	sprintf( dirname, "%s.dir", disk.shortname);

  if (mkdir( dirname, 0755) != 0) {
	perror( dirname);
	exit( 3);
  }

// copy all files in the directory created
  for (k = 1; k <= nfile; k++) {
	download( k, dirname);
  }
}

// Program starts here

int main( int argc, char **argv)
{
  int opt;
  char *filename;
  struct stat dsk_stat;
  int flags;

  while ((opt = getopt( argc, argv, "abhqv")) != -1) {
	switch (opt) {
	case 'h':
	  usage( *argv);
	  exit( 0);
	  break;
	case 'a':
	  all = 1;
	  break;
	case 'b':
	  where = 1;
	  break;
	case 'v':
	  verbose = 1;
	  break;
	case 'q':
	  quiet = 1;
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

  strncpy( disk.filename, filename, 256);
  disk.shortname = strrchr( disk.filename, '/');
  if (disk.shortname == NULL)
	disk.shortname = disk.filename;
  else
	disk.shortname++;

  disk.readonly = 1;
  disk.fd = open( disk.filename, O_RDONLY);
  disk.size = dsk_stat.st_size;
  if ((disk.dsk = malloc( disk.size)) == NULL) {
	perror( "Malloc failed: ");
	exit( 3);
  }
  if (read( disk.fd, disk.dsk, disk.size) < 0) {
    perror( filename);
	exit( 3);
  }
  close( disk.fd);

  return browse_dsk();
}
