/* flwrite.c -- Flex floppy image writing
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
static int force = 0;
static int delete = 0;
char **infile;

void usage( char *cmd) {
	fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
	fprintf( stderr, "       %s [-d] [-f] [-v] <infile>... <disk image>\n", cmd);
    fprintf( stderr, "Options:\n");
	fprintf( stderr, "   -d => delete infile from disk image, instead of copy them to\n");
	fprintf( stderr, "   -f => if disk image geometry is unusual, accept it and don't quit\n");
	fprintf( stderr, "   -v => print a listing of infile copied/deleted\n");
}

// Analyse the content of the disk loaded

int browse_dsk() {

  long size;
  int sector_size = SECSIZE;
  int nb_sectors;
  int dirsec;	// Number of directory sectors outside track 0
  int ibloc, freediff;
  uint8_t *current_sector, *dsk, *dir, *base, *entry, cftrk, cfsec;
  int i, j, k;
  char name[16];
  int errflag;
  struct tm dsktime, *ftime;
  uint8_t last_trk_sec;
  int found;
  int nb_blk;
  int notused = 0;

  struct stat inbuf;
  FILE *f_in;
  int nbf;
  int badname;
  char filename[10], extension[4];
  char ffname[16];
  char *fname, *ext;

  nb_sectors = disk.size / SECSIZE;
  if (nb_sectors * SECSIZE != disk.size) {
	fprintf( stderr, "ERROR: disk size don't match an integer number of sectors: %u bytes left\n",
		disk.size % SECSIZE);
	exit( 2);
  }

// Not a flex disk ?
  if (getname( disk.dsk + 0x210, disk.label, 0) < 0 || disk.dsk[0x226] == 0
	  || disk.dsk[0x227] == 0) {
	fprintf( stderr, "Not a Flex disk image: ");
	// Test OS/9
	size = (((long)disk.dsk[0]*256)+(long)disk.dsk[1])*256 + (long)disk.dsk[2];
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
	exit( 2);
  }

  disk.volnum = disk.dsk[0x21b]*256 + disk.dsk[0x21c];
// Size of disk & free sector list
  disk.nbtrk = disk.dsk[0x226];
  disk.nbsec = disk.dsk[0x227];
  disk.freesec = disk.dsk[0x221]*256 + disk.dsk[0x222];

// Too much free sectors for the disk ?
  if (disk.freesec > disk.nbtrk * disk.nbsec) {
	fprintf( stderr, "ERROR: Free sector size too big\n");
	exit( 2);
  }

  if ((disk.nbtrk+1) * disk.nbsec == nb_sectors) {
	if (verbose)
	  printf( "Single Density Disk detected\n");
	disk.track0l = disk.nbsec;
  } else {
	disk.track0l = nb_sectors - disk.nbtrk * disk.nbsec;
	if ((disk.nbsec >= 36 && disk.track0l == 20) ||
	  (disk.nbsec == 18 && disk.track0l == 10) ||
	  (disk.track0l == disk.nbsec/2)) {
	  if (verbose)
		printf ( "Double Density disk detected\n");
	} else {
	  if (force == 0)
		fprintf( stderr, "Unusual geometry found. Use -f to accept ");
	  else if (verbose == 1)
		fprintf( stderr, "Unusual geometry: ");
	  if (disk.track0l > disk.nbsec && disk.track0l <= 2 * disk.nbsec) {
		disk.track0l = disk.nbsec;
		disk.nbtrk++;
		last_trk_sec = nb_sectors - (disk.nbtrk-1) * disk.nbsec - disk.track0l;
		if (force == 0 || verbose == 1)	
		  fprintf( stderr, "%d sector track 0, plus a %d%s incomplete track of %d sectors added\n",
			disk.track0l, disk.nbtrk, "th", last_trk_sec);
	  } else if (disk.track0l > disk.nbsec/2 && disk.track0l < disk.nbsec) {
		if (force == 0 || verbose == 1)	
		  fprintf( stderr, "%d sectors/track, with a %d sectors track 0\n",
			disk.nbsec, disk.track0l);

	  } else {
		disk.nbtrk -= (((disk.nbtrk * disk.nbsec - nb_sectors) / disk.nbsec) + 1);
		if (force == 0 || verbose == 1)
		  fprintf( stderr, "number of tracks reduced to %d,", disk.nbtrk);
		if (disk.nbsec < 25)
	  	  disk.track0l = disk.nbsec;
		else
		  disk.track0l = nb_sectors - disk.nbtrk * disk.nbsec;
		if (force == 0 || verbose == 1)
		fprintf( stderr, " with a %d sectors track 0\n", disk.track0l);
	  }
	  if (force == 0)
		exit( 2);
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
	  fprintf( stderr, "ERROR: Bad freelist, sector out of bound\n");
	  exit( 2);
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
	  fprintf( stderr, "ERROR: Bad freelist, sector duplicated\n");
	  exit( 2);
	}
  }

  if (k != disk.freesec-1) {
	fprintf( stderr, "ERROR: Bad freelist, length doesn't match\n");
	exit( 2);
  }

  nfile = 1;
  nslot = 1;
  usedsec = 0;
  dirsec = 0;
  errflag = 0;

  ibloc = ts2blk( 0, 5);
  if (tabsec[ibloc] < -1)
	tabsec[ibloc] = 0; // value for directory bloc
  else {
	fprintf( stderr, "ERROR: Directory corrupted\n");
	exit( 2);
  }

  base = ts2pos( 0, 5) ; // dir on sector 5 (offset = 0x400)

  while (nslot < DIRSIZE) {  // TODO why limit this ? to be modified
	if (base[0])	// directory bloc ouside track 0
	  dirsec++;
	for (k=0; k < 10 && nslot < DIRSIZE; k++) {
  	  entry = base + 16 + (24 * k);
  	  if (*entry != 0) {
	    if (getname( entry, name, 1) < 0) {
		  file[nfile].flags |= 0x40;
		}
		j = ts2blk( entry[0xd], entry[0xe]);
		if (j < 1 || j == 2) {
		  file[nfile].length = 0;
		  file[nfile].flags |= 0x80;
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
		if (entry[0x17] > 50)
			dsktime.tm_year = (int)entry[0x17];
		else
			dsktime.tm_year = (int)entry[0x17] + 100;

		file[nfile].mtime = mktime( &dsktime);
		file[nfile].flags = 0;
		file[nfile].pos = entry;

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
  	  	nfile++; nslot++;
  	  } else {
		file[nslot].pos = entry;
		nslot++;
	  }
	}
    // Read next directory bloc
	if (base[0] == 0 && base[1] == 0)
	  break;
	if ((ibloc = ts2blk( base[0], base[1])) <0 ) {
	  fprintf( stderr, "ERROR: Directory corrupted\n");
	  exit( 2);
	}
	if (tabsec[ibloc] < -1)
      tabsec[ibloc] = 0;
	else {
	  fprintf( stderr, "ERROR: Directory corrupted\n");
	  exit( 2);
    }
    base = ts2pos( base[0], base[1]);
  }

// File linking analyse...
  nfile--;
  nslot--;

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
	  if (ts2blk( cftrk, cfsec) < 0) {	// bad file : sector out of bounds
		break;
	  }
	  ibloc = tabsec[ts2blk( cftrk, cfsec)];
	  current_sector = ts2pos( cftrk, cfsec);
	  nxtsec[ts2blk( cftrk, cfsec)] = ts2blk(current_sector[0], current_sector[1]);
	  nb_blk++;
	  if (ibloc < -1)
	    tabsec[ts2blk( cftrk, cfsec)] = k;
	  else if (ibloc == -1) {			// Bad file : bloc in freelist
		// => correct freelist and ignore file...
		file[k].flags |= 0x40;
	    tabsec[ts2blk( cftrk, cfsec)] = k;
	  } else if (ibloc == 0) {			// Bad file : override directory
		file[k].flags |= 0x80;
		fprintf( stderr, "ERROR: Mismatch between Directory and File sector allocation\n");
		exit( 2);
	  } else {							// Bad file : overlay other file
		file[k].flags |= 0x80;
		file[ibloc].flags |= 0x80;
	  }
	  cftrk = current_sector[0];
	  cfsec = current_sector[1];
	}
	if (nb_blk != file[k].length) {		// Bad file length
	  file[k].flags |= 0x40;
	}
  }

  for (k=5; k < nb_sectors; k++) {
	if (tabsec[k] == -99999) {
	  notused++;
	}
  }
  if (notused != 0) {
	fprintf( stderr, "ERROR: %d sector(s) missing in freelist\n", notused);
	exit( 2);
  }

  if (delete == 0) {

// add infile to disk image
	for (j = 0; infile[j] != NULL; j++) {

// Is a directory entry available ?
	  if (nfile >= nslot) {
		printf( "ERROR : No more directory entry available.\n");
		exit( 2);
	  }

// Flex name must be 8+3, start by a letter, then only [-_A-Za-z0-9]
	  fname = strrchr( infile[j], '/');
	  if (fname == NULL)
		fname = infile[j];
	  else
		fname++;

	  if (isalpha( fname[0])) { // Start by 'x' if first char not a letter
		filename[0] = fname[0];
		badname = 0;
	  } else {
		filename[0] = 'x';
		badname = 1;
	  }
	  for (i = 1; fname[i] != 0; i++) {	// replace invalid chars by '_'
		if (isalnum( fname[i]) || fname[i] == '-' || fname[i] == '_')
		  filename[i] = fname[i];
		else {
		  if (fname[i] == '.') {
			filename[i] = 0;
			break;
		  }
		  filename[i] = '_';
		  badname |= 2;
		}
		if (i == 7) {
		  filename[8] = 0;
		  if (fname[i+1] != '.')
			badname |= 4;
		  break;
		}
	  }
	  if ((ext = strrchr( fname, '.')) == NULL)
		extension[0] = 0;
	  else {
		for (i = 0; i < 3; i++) {
		  if (isalnum( ext[i+1]) || ext[i+1] == '-' || ext[i+1] == '_')
			extension[i] = ext[i+1];
		  else if (ext[i+1] == 0)
			break;
		  else {
			extension[i] = '_';
			badname |= 2;
		  }
		}
		if (ext[i+1] != 0)
		  badname |= 8;
		extension[i] = 0;
	  }

// Existing file of same name ?
	  strcpy( ffname, filename);
	  if (extension[0] != 0) {
		strcat( ffname, ".");
		strcat( ffname, extension);
	  }
	  for (k = 1; k <= nfile; k++)
		if (strcmp( file[k].name, ffname) == 0) {
		  badname |= 0x80;
		  break;
		}
	  if ((badname & 0x80) != 0) {
		printf( "Can't copy %s", infile[j]);
		if ((badname & 0x7F) != 0)
		  printf( " as %s", ffname);
		printf( " : file exists.\n");
		continue;
	  }

// Verify free space
	  if (stat( infile[j], &inbuf) < 0) {
		perror( infile[j]);
		continue;
	  }

	  if ((inbuf.st_mode & S_IFMT) != S_IFREG) {
		printf( "File %s is not a regular file: ignored.\n", infile[j]);
		continue;
	  }

	  nbf = inbuf.st_size / 252;
	  if (inbuf.st_size % 252 != 0) {
		nbf++;
		if (verbose)
		  printf( "Padding file %s with '0's.\n", infile[j]);
	  }
	  if (disk.freesec < nbf) {
		printf( "Not enough space to copy %s, skipping it.\n", infile[j]);
		continue;
	  }

	  if ((f_in = fopen( infile[j], "r")) == NULL) {
		perror( infile[j]);
		continue;

	  }

// Find first free directory entry, update directory and free list size
	  nfile++;
	  entry = file[nfile].pos;
	  for (i = 0; i < 24; i++)	// make it clean
		entry[i] = 0;

	  file[nfile].length = nbf;
	  entry[0x12] = nbf & 0xFF;
	  if (nbf > 256)
		entry[0x11] = (uint8_t) (nbf / 256);
	  disk.freesec -= nbf;
	  disk.dsk[0x221] = (uint8_t) (disk.freesec / 256);
	  disk.dsk[0x222] = (uint8_t) (disk.freesec % 256);

// Fill it with name, length, start sector (first of free list), and date
	  strcpy (file[nfile].name, ffname);
	  strcpy (entry, filename);
	  strcpy (entry + 8, extension);

	  file[nfile].mtime = inbuf.st_mtime;
	  ftime = localtime( &file[nfile].mtime);
	  entry[0x15] = (uint8_t) ftime->tm_mon + 1;
	  entry[0x16] = (uint8_t) ftime->tm_mday;
	  entry[0x17] = (uint8_t) ftime->tm_year;

	  cftrk = disk.dsk[0x21d];
	  cfsec = disk.dsk[0x21e];
	  file[nfile].start_trk = cftrk;
	  file[nfile].start_sec = cfsec;
	  entry[0xd] = cftrk;
	  entry[0xe] = cfsec;
	  current_sector = ts2pos( cftrk, cfsec);

// Copy sectors.
	  for (i = 0; i < nbf; i++) {
		for (k = 2; k < SECSIZE; k++)	// Clean sector
		  current_sector[k] = 0;
		fread( current_sector + 4, 1, 252, f_in);
// If random file, manage differently the 2 first sectors
		if (i == 0 && strcmp( current_sector + 4, "#FLEX##RAND#") == 0) {
		  entry[0x13] = 2;
		  for (k = 4; k < 16; k++)
			current_sector[k] = 0;
		  current_sector = ts2pos( current_sector[0], current_sector[1]);
		  continue;
		}
		if (i > 1 || entry[0x13] == 0) {
		  current_sector[3] = (uint8_t) ((i + 1 - entry[0x13]) & 0xFF);
		  if (i > 255)
			current_sector[2] = (uint8_t) (((i + 1 - entry[0x13]) % 256) & 0xFF);
		}
		if (i < nbf - 1) {
		  cftrk = current_sector[0];
		  cfsec = current_sector[1];
		  current_sector = ts2pos( cftrk, cfsec);
		}
	  }

// Update free sector list start, update link of file's last sector
	  entry[0x0f] = cftrk;
	  entry[0x10] = cfsec;
      disk.dsk[0x21d] = current_sector[0];
      disk.dsk[0x21e] = current_sector[1];
	  current_sector[0] = 0;
	  current_sector[1] = 0;

// If random file, verify sectors continuity and update first 2 sectors
	  if (entry[0x13] == 2) {
		current_sector = ts2pos( entry[0xd], entry[0xe]);	// index sector
		base = current_sector + 4;
		ibloc = ts2blk( current_sector[0], current_sector[1]);
		ibloc = nxtsec[ibloc];	// first data sector
		base[0] = blk2trk( ibloc);
		base[1] = blk2sec( ibloc);
		k = 1;
		for (i = 0; i < nbf - 3; i++) {
		  if (nxtsec[ibloc] == ibloc + 1) {
			k++;
			ibloc++;
		  } else if (nxtsec[ibloc] > 0) {
			base[2] = k;
			ibloc = nxtsec[ibloc];
			base += 3;
			if (base - current_sector >= 256) {
			  current_sector = ts2pos( current_sector[0], current_sector[1]);
			  base = current_sector + 4;
			}
			base[0] = blk2trk( ibloc);
			base[1] = blk2sec( ibloc);
			k = 1;
		  } else
			break;
		}
		base[2] = k;
	  }
	  
	  fclose( f_in);
	
	  if ((badname & 0x7F) != 0 || verbose)
		printf( "copying file %s", infile[j]);
	  if (badname != 0) {
		printf( " as %s", filename);
		if (extension[0] != 0)
		  printf( ".%s", extension);
		printf( " to respect Flex filename rules");
	  }
	  if (badname != 0 || verbose)
		putchar( '\n');
	}

  } else {

// delete infile from disk image
	for (j = 0; infile[j] != NULL; j++) {
	  found = 0;
	  for (k = 1; k <= nfile; k++) {
		if (strcmp( file[k].name, infile[j]) == 0) {
		  // First char of name becomes $FF
		  entry = file[k].pos;
		  entry[0] = 0xFF;
		  // Number of free sectors += size of file
		  disk.freesec += file[k].length;
		  disk.dsk[0x221] = (uint8_t) (disk.freesec / 256);
		  disk.dsk[0x222] = (uint8_t) (disk.freesec % 256);
		  // End of Freesector list point to start of file
		  current_sector = ts2pos( disk.dsk[0x21f], disk.dsk[0x220]);
		  current_sector[0] = file[k].start_trk;
		  current_sector[1] = file[k].start_sec;
		  // New end of Freesector list
		  disk.dsk[0x21f] = file[k].end_trk;
		  disk.dsk[0x220] = file[k].end_sec;

		  found = 1;
		  if (verbose)
			printf( "File %s deleted\n", infile[j]);
		  break;
		}
	  }
	  if (found == 0) {
		fprintf( stderr, "File to delete not found : %s\n", infile[j]);
		retval = 1;
	  }
	}
  }

  if (msync( disk.dsk, disk.size, MS_SYNC) < 0) {
	perror( "Error: Update of disk image failed");
	return 2;
  }
  return munmap( disk.dsk, disk.size);
}

// Program start here
int main( int argc, char **argv)
{
  int opt;
  char *diskname;
  struct stat dsk_stat;
  int flags;
  int i;

  while ((opt = getopt( argc, argv, "hvfd")) != -1) {
	switch (opt) {
	case 'h':
	  usage( *argv);
	  exit( 0);
	  break;
	case 'v':
	  verbose = 1;
	  break;
	case 'd':
	  delete = 1;
	  break;
	case 'f':
	  force = 1;
	  break;
	default: /* '?' */
	  usage( *argv);
	  exit( 3);
	}
  }

  if (strcmp( argv[0], "fldel") == 0)
	delete = 1;

  if (argc - optind < 2) {
	fprintf( stderr, "Not enough file names...\n");
	usage( *argv);
	exit( 2);
  }

// creating file list
  infile = (char **) malloc( sizeof( uint8_t *) * (argc - optind));
  i = 0;
  while (optind < argc - 1) {
	infile[i] = argv[optind];
	optind++;
	i++;
  }
  infile[i] = NULL;

// Checking disk image
  strncpy( disk.filename, argv[argc-1], 256);
  if (stat( disk.filename, &dsk_stat)) {
	perror( disk.filename);
	exit( 2);
  }

  disk.shortname = strrchr( disk.filename, '/');
  if (disk.shortname == NULL)
	disk.shortname = disk.filename;
  else
	disk.shortname++;

  if ((disk.fd = open( disk.filename, O_RDWR)) < 0 ) {
	perror( disk.filename);
	exit( 2);
  }

  disk.readonly = 0;
  flags = PROT_READ | PROT_WRITE;
  disk.size = dsk_stat.st_size;
  disk.dsk = mmap( &disk.dsk, dsk_stat.st_size, flags, MAP_SHARED, disk.fd, 0);
  close( disk.fd);

  if (disk.dsk == NULL) {
	perror( diskname);
	exit( 3);
  }

  return browse_dsk();
}
