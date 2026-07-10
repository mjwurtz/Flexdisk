/* flread.c -- Flex floppy file extraction
   Copyright (C) 2026 Michel Wurtz - mjwurtz@gmail.com

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
char fname[16]; // Flex 'name.ext' used (Upper case name)

char *s_err,     // If color is supported => errmsg in red
     *s_warn,    // warnings in yellow
	 *s_ok,		 // OK
     *s_norm;    // return to normal

void usage( char *cmd) {
	fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
	fprintf( stderr, "       %s [-c] [-o] [-v] <files>... <disk image>\n", cmd);
    fprintf( stderr, "Options:\n");
	fprintf( stderr, "   -c => convert text files from Flex to Unix format\n");
	fprintf( stderr, "   -o => if a file exists, don't ignore it but replace it\n");
	fprintf( stderr, "   -v => print some details and a listing of files on image\n");
}

// Extract file from disk image

int extract_file( char *name, int replace, int convert) {

  FILE *out;
  struct Entry *entry;
  struct stat file_stat;
  uint8_t *current;
  uint8_t cftrk, cfsec;
  int j, k;
  struct utimbuf new_times;
  struct tm dsktime;
  time_t itime;
  int nb_blk;
  int state;		// manage flex text compression
  int c;
  
// Does file exist ?

  if (strlen( name) > 12)  // incompatible length
    return 1;
  for (k = 0; k < strlen( name); k++)
	fname[k] = toupper( name[k]);
  fname[k] = 0;
// Find file
  k = 0;
  while (strcmp( file[k].name, fname) != 0 && k < nslot)
	 k++;
  if (k == nslot)  // not found
    return 1;

  if (file[k].name[0] == '?' || (file[k].flags & 0x80) != 0)
	return 2;

  if (convert)
	for (j = 0; j < 12; j++)
      fname[j] = tolower( file[k].name[j]);

  if (stat( fname, &file_stat) == 0 && replace == 0)
	return 3;
  if ((out = fopen( fname, "wb")) == NULL) {
	perror( fname);
	return 4;
  }
  cftrk = file[k].start_trk;
  cfsec = file[k].start_sec;
  current = ts2pos( cftrk, cfsec);
  if (file[k].flags & 0x02) {
	fputs( "#FLEX##RAND#", out);
	j = 16;
  } else
	j = 4;

  nb_blk = 0;
  state = 0;
  while (nb_blk < file[k].length) {
	if (ts2blk( cftrk, cfsec) < 1) {
	  break;
	}
	current = ts2pos( cftrk, cfsec);
	nb_blk++;
	cftrk = current[0];
	cfsec = current[1];
	while (j < 256) {
	  if (convert) {
		c = current[j++];
		if (state == 0) {
		  if (c == 0x33)           // ESC char ?
			state = -1;
		  else if (c != 0) {       // ignore NULL
								   // end of line vs OS...
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(APPLE)
	  		fputc( c, out);
#endif
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__linux__) || defined(__gnu_linux__)
			if (c == '\r')         // CR ?
			  fputc( '\n', out);   // add NL
#endif
#if defined(__linux__) || defined(__gnu_linux__)
			else
	  		  fputc( c, out);
#endif
		  }
		} else {
		  state = 0;       // number of space to print
		  while (c-- > 0)
			putchar( ' ');
		}
	  } else
		fputc( current[j++], out);
	}
	j = 4;
  }
  fclose( out);
  dsktime.tm_hour = 12;
  dsktime.tm_min = 0;
  dsktime.tm_sec = 0;
  dsktime.tm_isdst = 0;
  dsktime.tm_mday = file[k].day;
  dsktime.tm_mon = file[k].month-1;
  dsktime.tm_year = file[k].year-1900;  // Why -1900 ???
  itime = mktime( &dsktime);

  new_times.actime = time( NULL);
  new_times.modtime = itime;
  utime( fname, &new_times);

  if (cftrk != 0 || cfsec != 0 || nb_blk != file[k].length)
	printf( "Warning! file '%s' may be truncated...\n", fname);

  return 0;
}

// Program start here
int main( int argc, char **argv)
{
  char *term, *getenv( const char *name);
  int opt;
  char filepath[256];
  struct stat dsk_stat;
  int retval, done;
  int strict = 0;
  int convert = 0;
  int overwrite = 0;
  char **infile;
  int i;

  while ((opt = getopt( argc, argv, "hvco")) != -1) {
	switch (opt) {
	case 'h':
	  usage( *argv);
	  exit( 0);
	  break;
	case 'v':
	  verbose++;
	  break;
	case 'c':
	  convert = 1;
	  break;
	case 'o':
	  overwrite = 1;
	  break;
	default: /* '?' */
	  usage( *argv);
	  exit( 3);
	}
  }

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
  if (retval > 1 && retval != 257)
	return retval;

  if ((retval = analyse( 0)) > 1)
	return retval;

  if (verbose) {
    putchar( '\n');
	list_files( verbose-1);
  }

  for (i = 0; infile[i] != NULL; i++) {
	done = extract_file( infile[i], overwrite, convert);
	switch (done) {
	  case 1:  printf( "%sERROR: File '%s' not found.%s\n", s_err, infile[i], s_norm);
		       break;
	  case 2:  printf( "%sERROR: File '%s' is marked deleted.%s\n", s_err, infile[i], s_norm);
		       break;
	  case 3:  printf( "%sWarning: file '%s' allready present.%s\n", s_warn, fname, s_norm);
			   printf( "Use -o option to overwrite it\n");
			   break;
	  case 4:  printf( "%sERROR: can't create file '%s'.%s\n", s_err, fname, s_norm);
			   break;
	  default: if (verbose)
			   printf( "%sFile '%s' copied%s\n", s_ok, fname, s_norm);
	  }
      retval |= done;
  }
  return retval;
}
