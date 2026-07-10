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

#include "dskflex.h"

int verbose = 0; // more details when verbose increase
int quiet = 1;   // by default don't give disk infos
char ffname[16]; // Flex 'name.ext' used (Upper case name)

char *s_err,     // If color is supported => errmsg in red
     *s_warn,    // warnings in yellow
	 *s_ok,		 // OK
     *s_norm;    // return to normal

void usage( char *cmd) {
	fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
	fprintf( stderr, "       %s [-d] [-f] [-o] [-v] <infile>... <disk image>\n", cmd);
    fprintf( stderr, "Options:\n");
	fprintf( stderr, "   -d => delete infile from disk image, instead of copy them to\n");
	fprintf( stderr, "   -f => if disk image geometry is unusual, accept it and don't quit\n");
	fprintf( stderr, "   -o => if a file exists on image, don't ignore it but replace it\n");
	fprintf( stderr, "   -v => print a listing of infile copied/deleted\n");
}

// Modify the content of the disk loaded

// Delete file from disk image

int delete_file( char *name) {

  struct Entry *entry;
  uint8_t *current_sector;
  int k;
  char fname[16];

// delete file from disk image

  if (strlen( name) > 12)
    return 1;
  for (k = 0; k < strlen( name); k++)
	fname[k] = toupper( name[k]);
  fname[k] = 0;

  k = 0;
  while (strcmp( file[k].name, fname) != 0 && k < nslot) {
	 k++;
  }
  if (k == nslot)
    return 1;
  // First char of name becomes $FF
  entry = (struct Entry *)file[k].pos;
  entry->name[0] = 0xFF;
  file[k].flags |= 0x10;
  file[k].name[0] = '?';
  nfile--;
  ndel++;
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

  return 0;
}

// Add file to disk image

int insert_file( char *name, int replace) {
  struct Entry *entry; // directory entry
  int retval;        // Return value
  int i, j, k;       // loo indexes

  char *fname;       // Unix file name
  char filename[10]; // Flex file name
  char extension[4]; // Flex file extension
  char *ext;
  FILE *f_in;        // Original Unix file name
  int fsize;         // Size of file
  int random;        // Copy random file ?
  int nbf;           // Number of blocs needed to copy
  char magic[13];    // fila start with "" if random
  uint8_t cfsec, cftrk;
  uint8_t *base;
  int ibloc;

  struct stat inbuf; // unix file metadata

  struct tm *ftime;
  uint8_t *current_sector;

// Flex name must be 8+3, start by a letter, then only [-_A-Z0-9]
  fname = strrchr( name, '/');
  if (fname == NULL)
	fname = name;
  else
	fname++;

  if (isalpha( fname[0])) { // Start by 'x' if first char not a letter
	filename[0] = toupper(fname[0]);
	retval = 0;
  } else {
	filename[0] = 'X';
	retval = 1;
  }
  for (i = 1; fname[i] != 0; i++) {	// replace invalid chars by '_'
	if (isalnum( fname[i]) || fname[i] == '-' || fname[i] == '_')
	  filename[i] = toupper(fname[i]);
	else {
	  if (fname[i] == '.') {
		filename[i] = 0;
		break;
	  }
	  filename[i] = '_';
	  retval |= 2;
	}
	if (i == 7) {
	  filename[8] = 0;
	  if (fname[i+1] != '.')
		retval |= 4;
	  break;
	}
  }
  if ((ext = strrchr( fname, '.')) == NULL) {
	extension[0] = 'X';
    extension[1] = '_';
    extension[2] = '_';
    extension[3] = 0;
  } else {
	for (i = 0; i < 3; i++) {
	  if (isalnum( ext[i+1]) || ext[i+1] == '-' || ext[i+1] == '_')
		extension[i] = toupper(ext[i+1]);
	  else if (ext[i+1] == 0)
		break;
	  else {
		extension[i] = '_';
		retval |= 2;
	  }
	}
	if (ext[i+1] != 0)
	  retval |= 8;
	extension[i] = 0;
  }
// Existing file of same name ?
  strcpy( ffname, filename);
  strcat( ffname, ".");
  strcat( ffname, extension);
  k = 0;
  while (strcmp( file[k].name, ffname) != 0 && k < nslot)
	 k++;
  if (k != nslot)
	if (replace == 0)
      return 0x20;
	else if (delete_file( fname))
	  return 0x30;
// Is a directory entry available ?
  if (nfile >= nslot)
	return 0x10;

// Verify free space
  if (stat( name, &inbuf) < 0) {
	if (verbose)
	  perror( name);
	return 0x40;
  }

  if ((inbuf.st_mode & S_IFMT) != S_IFREG)
	return 0x50;

  if ((f_in = fopen( name, "r")) == NULL) {
	if (verbose)
	  perror( name);
	return 0x40;
  }

  // Test if a file saved by fldump or flread is random
  
  nbf = inbuf.st_size / 252;
  if (fsize % 252 != 0) {
	nbf++;
	retval |= 8;
	if (verbose > 1)
	  printf( "Padding file '%s' with '0's.\n", name);
  }
  if (disk.freesec < nbf)
	return 0x60;

// Find first free directory entry, or first deleted entry
  if (ndel + nfile == nslot)
 	k = nslot;
  else
    k = 0;
  while (k < nslot && file[k].flags != 0)
	k++;
  if (k == nslot) {
	k = 0;
	while (k < nslot && (file[k].flags & 0x10) != 0x10)
	  k++;
  }
  if (k == nslot)
	return 0x10;
  if ((file[k].flags & 0x10) != 0x10)
	ndel--;

// Update directory entry found and Sir
  nfile++;
  entry = (struct Entry *)file[k].pos;

  disk.freesec -= nbf;
  disk.dsk[0x221] = (uint8_t) (disk.freesec / 256);
  disk.dsk[0x222] = (uint8_t) (disk.freesec % 256);

// Fill name, length, start sector (first of free list), and date
  file[k].length = nbf;
  entry->length[1] = nbf & 0xFF;
  if (nbf > 256)
	entry->length[0] = (uint8_t) (nbf / 256);

  strcpy( file[k].name, ffname);
  file[k].flags = 1;

  strcpy (entry->name, filename);
  for (i = strlen( filename); i < 8; i++)
	entry->name[i] = 0;
  strcpy (entry->ext, extension);
  for (i = strlen( extension); i < 3; i++)
	entry->ext[i] = 0;

  ftime = localtime( &inbuf.st_mtime);
  file[k].day = (uint8_t) ftime->tm_mday;
  entry->f_day = file[k].day;
  file[k].month = (uint8_t) ftime->tm_mon + 1;
  entry->f_month = file[k].month;
  file[k].year = (uint8_t) ftime->tm_year;
  entry->f_year = (uint8_t)(file[k].year & 0xFF);

  cftrk = disk.dsk[0x21d];
  cfsec = disk.dsk[0x21e];
  file[k].start_trk = cftrk;
  file[k].start_sec = cfsec;
  entry->first_trk = cftrk;
  entry->first_sec = cfsec;
  current_sector = ts2pos( cftrk, cfsec);

// Copy sectors.
  for (i = 0; i < nbf; i++) {
	for (k = 2; k < SECSIZE; k++)	// Clean sector
	  current_sector[k] = 0;
	j=fread( current_sector + 4, 1, 252, f_in);
// If random file, manage differently the 2 first sectors
	if (i == 0 && strcmp( current_sector + 4, "#FLEX##RAND#") == 0) {
      file[k].random = 2;
      entry->flags = 2;
	  for (k = 4; k < 16; k++)
		current_sector[k] = 0;
	  current_sector = ts2pos( current_sector[0], current_sector[1]);
	  continue;
	}
	if (i > 1 || entry->flags == 0) {
	  current_sector[3] = (uint8_t) ((i + 1 - entry->flags) & 0xFF);
	  if (i > 255)
		current_sector[2] = (uint8_t) (((i + 1 - entry->flags) % 256) & 0xFF);
	}
	if (i < nbf - 1) {
	  cftrk = current_sector[0];
	  cfsec = current_sector[1];
	  current_sector = ts2pos( cftrk, cfsec);
	}
  }

// Update free sector list start, update link of file's last sector
	  entry->last_trk = cftrk;
	  entry->last_sec = cfsec;
      disk.dsk[0x21d] = current_sector[0];
      disk.dsk[0x21e] = current_sector[1];
	  current_sector[0] = 0;
	  current_sector[1] = 0;

// If random file, verify sectors continuity and update first 2 sectors
	  if (random) {
		current_sector = ts2pos( entry->first_trk, entry->first_sec);	// index sector
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
	  return retval;
}

// Program start here
int main( int argc, char **argv)
{
  char *term, *getenv( const char *name);
  int opt;
  char filepath[256];
  char *backup;
  struct stat dsk_stat;
  int retval, done;
  int strict = 1;
  int force = 0;
  int delete = 0;
  int overwrite = 0;
  char **infile;
  int i;

  while ((opt = getopt( argc, argv, "hvdfo")) != -1) {
	switch (opt) {
	case 'h':
	  usage( *argv);
	  exit( 0);
	  break;
	case 'v':
	  verbose++;
	  break;
	case 'd':
	  delete = 1;
	  break;
	case 'f':
	  force = 1;
	  break;
	case 'o':
	  overwrite = 1;
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

// If possible, colorize Warnings and Errors
  if (isatty( 1) && (term = getenv( "TERM")) != NULL) {
    if (strstr( term, "256color") != NULL) {
	  s_ok = "\e[1;92m";
      s_warn = "\e[1;93m";
      s_err  = "\e[1;91m";
      s_norm = "\e[0m";
    } else {
      s_err = s_warn = s_norm = "";
    }
  }

// Checking disk image
  strncpy( filepath, argv[argc-1], 256);
  if (stat( filepath, &dsk_stat)) {
	perror( filepath);
	exit( 2);
  }

  disk.shortname = strrchr( filepath, '/');
  if (disk.shortname == NULL)
	disk.shortname = filepath;
  else
	disk.shortname++;

  if ((dsk_stat.st_mode & S_IWUSR) == 0) {
    fprintf( stderr, "ERROR: %s is not writable!\n", disk.shortname);
	exit( 2);
  }

  if ((disk.fd = open( filepath, O_RDONLY)) < 0 ) {
	perror( filepath);
	exit( 2);
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

  // Is it a clean flex image ?

  if (! isFlex( disk.dsk, disk.nb_sectors))
	exit( 2);

  retval = badFlex( 1);
  if (retval == 257) {
  	if (force == 0) {
	  fprintf( stderr, "Unusual geometry found. Use -f to accept it\n");
	  exit( 2);
	} else if (verbose)
	  fprintf( stderr, "Unusual geometry. No garanty...\n");
  } else if (retval) {
	fprintf( stderr, "Bad Flex image.  Correct it before use\n");
	exit( 2);
  }

  retval = analyse( 1);
  if (retval> 1)
	return retval;
  else if (retval == 1)
	printf( "%sCould be corrected, process anyway...%s\n", s_warn, s_norm);

  if (verbose)
    putchar( '\n');

  for (i = 0; infile[i] != NULL; i++) {
	if (delete) {
	  done = delete_file( infile[i]);
	  if (verbose)
	    if (done == 0)
	      printf( "%sFile '%s' deleted%s\n", s_ok, infile[i], s_norm);
	    else
		  printf( "%sFile '%s' not found !%s\n", s_warn, infile[i], s_norm);
	} else {
		done = insert_file( infile[i], overwrite);
		switch (done & 0xF0) {
		  case 0x10: printf( "%sERROR: No more directory entry available.%s\n", s_err, s_norm);
					 break;
		  case 0x20: printf( "%sWarning: file '%s' allready present on image", s_warn, infile[i]);
					 if (done & 0xF)
					   printf( " under the name '%s'.%s\n", ffname, s_norm);
					 else
					   printf( ".%s\n", s_norm);
					 printf( "Use -o option to overwrite it\n");
					 break;
		  case 0x30: printf( "%sERROR: Unable to delete '%s', file '%s' not copied.%s\n",
					        s_err, ffname, infile[i], s_norm);
   					 break;
		  case 0x40: printf( "%sERROR: can't stat/open file '%s'.%s\n", s_err, infile[i], s_norm);
					 break;
		  case 0x50: printf( "%sERROR: File '%s' is not a regular file: ignored.%s\n",
							s_err, infile[i], s_norm);
					 break;
		  case 0x60: printf( "%sERROR: Not enough space left to copy '%s', skipping it.%s\n",
							s_err, infile[i], s_norm);
					 break;
		  default:   if (verbose) {
					   printf( "%sFile '%s' copied", s_ok, infile[i]);
					   if (done & 0x0F)
						 printf( " as '%s'.%s\n", ffname, s_norm);
					   else
						 printf( ".%s\n", s_norm);
					 }
		}
	}
    retval |= done;
  }

  if (verbose)
	list_files( verbose-1);

  // rename original file
  backup = malloc( strlen( filepath) + 4);
  strcpy( backup, filepath);
  strcat( backup, ".bak");
  if (rename( filepath, backup)) {
	perror( "backup: ");
	return 3;
  }

  // create modified file
  disk.fd = creat( filepath, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

  if (disk.fd == -1) {
    perror( "create: ");
    exit( 3);
  }

  if (write( disk.fd, disk.dsk, dsk_stat.st_size) != dsk_stat.st_size) {
	perror( "rewrite: ") ;
	exit( 3);
  }
  close( disk.fd);
  if (retval & 0xF0)
	return 2;
  else if (retval & 0x0F)
	return 1;
  else
    return 0;
}
