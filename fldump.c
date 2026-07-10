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

#include "dskflex.h"

char *s_err,     // If color is supported => errmsg in red
     *s_warn,    // warnings in yellow
     *s_norm;    // return to normal

int verbose = 0;
int quiet = 0;
int all = 0;
int where = 0;

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
  struct tm dsktime;
  time_t itime;

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
  dsktime.tm_hour = 12;
  dsktime.tm_min = 0;
  dsktime.tm_sec = 0;
  dsktime.tm_isdst = 0;
  dsktime.tm_mday = file[index].day;
  dsktime.tm_mon = file[index].month-1;
  dsktime.tm_year = file[index].year-1900;  // Why -1900 ???
  itime = mktime( &dsktime);

  new_times.actime = time( NULL);
  new_times.modtime = itime;
  utime( path, &new_times);

  if (cftrk != 0 || cfsec != 0 || nb_blk != file[index].length)
	printf( "Warning! file '%s' may be truncated...\n", path);
}

// Program starts here

int main( int argc, char **argv)
{
  int opt;
  char *filepath;
  int retval = 0;
  struct stat dsk_stat;
  int flags;
  int k;
  char dirname[32];
  char *term, *getenv( const char *name);

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

// Some sanitary checking on options

  if (quiet && verbose) {
	fprintf( stderr, "Options '-q' and '-v' are exclusive, '-q' ignored\n");
	quiet = 0;
  }

  if (optind < argc) {
	filepath = argv[ optind++];
	if (optind < argc) {
	  fprintf( stderr, "Only one file is allowed\n");
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

  disk.shortname = strrchr( filepath, '/');
  if (disk.shortname == NULL)
	disk.shortname = filepath;
  else
	disk.shortname++;

  disk.readonly = 1;
  disk.fd = open( filepath, O_RDONLY);
  disk.size = dsk_stat.st_size;
  if ((disk.dsk = malloc( disk.size)) == NULL) {
	perror( "Malloc failed: ");
	exit( 3);
  }
  if (read( disk.fd, disk.dsk, disk.size) < 0) {
    perror( filepath);
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

// Print file list...
  if (!quiet) 
    list_files( verbose);

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
  for (k = 0; k < nfile; k++) {
	download( k, dirname);
  }

  return retval;
}
