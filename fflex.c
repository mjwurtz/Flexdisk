/* fflex.c -- Flex floppy formating 
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

#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

//#include "dsk.h"

#define VERSION "1.1 (2024-06-30)"

void usage( char *cmd) {
	printf( "Usage : %s [options...] <filename>\n", cmd);
	printf( "Options :\n");
	printf( "  -h --help : this help\n");
	printf( "  -l --label=<volume label> (default = filename)\n");
	printf( "  -v --volume-number=<0-65535> (default = 0)\n");
	printf( "  -t --track-count=<number of tracks> (default 40, max 256)\n");
	printf( "  -s --sector-count=<number of sectors> (default 10, max 255)\n");
	printf( "  -d --double-density  (default single density)\n");
	printf( "  -f --first-track=<number of sectors in first track if double-density>\n");
	printf( "     (same number for single side and (number/2 + 2) for double side)\n");
	printf( "  -g --geometry=[SS|DS][SD|DD][40|80] (standard flex formats for 5\" floppy)\n");
	printf( "     example : DSSD80 for a double side single density 80 track floppy\n");
	printf( "     if this option is used, options -t, -s, -f and -d are ignored\n");
	printf( "If no extension is given, '.dsk' is used.\n");
	printf( "Flex volume name is limited to the 11 first chars of the -l parameter, or\n");
	printf( "if -l not present, the first 11 chars of the filename, without extension.\n");
	exit( EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char filename[256];
	char geometry[10];
	char volname[12], *ext;
	// Some defaul values...
	int dsknum = 1,
		nbtrk = 40,
		nbsec = 10,
		dd = 0,
		ft = 0,
		fd, nbfree; 
	int erg = 0; // error in geometry string ?

	// Sector buffer
	uint8_t bloc[256];

	// Time
	struct tm *createdate;
	time_t tloc;

	int i, j;

	static struct option fopt[] = {
		{"help",           no_argument,       0, 'h' },
		{"label",          required_argument, 0, 'l' },
		{"volume-number",  required_argument, 0, 'v' },
		{"track-count",    required_argument, 0, 't' },
		{"sector-count",   required_argument, 0, 's' },
		{"first-track",    required_argument, 0, 'f' },
		{"double-density", no_argument,       0, 'd' },
		{"geometry",       required_argument, 0, 'g' },
		{0,                0,                 0, 0 }
	} ;
	int opt, val;
	int opt_index = 0;

	*volname = 0;
	*geometry = 0;

	printf( "Fflex version %s\n", VERSION);

	while ((opt = getopt_long(argc, argv, "hl:v:t:s:f:dg:", fopt, &opt_index)) != -1) {
		switch (opt) {
		case 'l':
			if (optarg) {
				strncpy( volname, optarg, 12);
				volname[11]=0;
			}
			break;
		case 'v':
		    sscanf( optarg, "%d", &dsknum);
		    break;

		case 't':
		    sscanf( optarg, "%d", &nbtrk);
		    break;

		case 's':
		    sscanf( optarg, "%d", &nbsec);
		    break;

		case 'f':
		    sscanf( optarg, "%d", &ft);
		    break;

		case 'd':
			dd = 1;
		    break;

		case 'g':
			if (optarg) {
				strncpy( geometry, optarg, 9);
				geometry[9] = 0;
			}
		    break;

		case 'h':
		case '?':
			usage( *argv);
			break;

		default:
		    printf("?? getopt returned character code 0x%02X ??\n", *optarg);
			usage( *argv);
		}
	}

// Some sanitary checking
	if (optind < argc) {
		strncpy( filename, argv[optind++], 250);
		filename[250] = 0;
		if (optind < argc) {
			printf( "Warning : too many filenames, last ones ignored:");
			while( optind < argc)
				printf( " %s", argv[optind++]);
			putchar('\n');
		}
	} else {
		printf( "A file name is mandatory.\n");
		usage( *argv);
	}

	// Correct volname if needed

	for (i = 0; volname[i]; i++) {
		if (!isalnum(volname[i]) && volname[i] != '-' && volname[i] != '_') {
			printf( "Illegal character '%c' in Volume name, replaced by '_'\n", volname[i]);
			volname[i] = '_';
		}
	}
	if (*volname == 0) {
		for (i = 0; filename[i] && filename[i] != '.'; i++) {
			if (!isalnum(filename[i]) && filename[i] != '-' && filename[i] != '_')
				volname[i] = '_';
			else
				volname[i] = filename[i];
		}	
	}
	while (i < 12)
		volname[i++] = 0;

// Is Volume number OK ?	
	if (dsknum  < 0 || dsknum > 0xFFFF) {
		printf( "Disk Volume number (%d) must be positive and less than 65536\n", dsknum);
		usage( *argv);
	}

	if (*geometry) {
		if ((geometry[1] & 0x5F) != 'S' || (geometry[3] & 0x5F) != 'D'
			|| geometry[5] != '0')
			erg = 1;

		if ((geometry[2] & 0x5F) == 'D')
			dd = 1;
		else if ((geometry[2] & 0x5F) != 'S')
			erg = 1;

		if (geometry[4] == '8')
			nbtrk = 80;
		else if (geometry[4] == '4')
			nbtrk = 40;
		else
			erg = 1;

		if ((*geometry & 0x5F) == 'S')
			nbsec = ft = 10 * (dd+1);
		else if ((*geometry & 0x5F) == 'D') {
			nbsec = 18 * (dd+1);
			ft = 10 * (dd+1);
		} else
			erg = 1;

		if (erg) {
			printf( "Geometry string '%s' not valid.\n", geometry);
			usage( *argv);
		}
	} else {
		if (nbsec > 255 || nbsec < 6 + 2 * dd) {
			printf( "Number of sectors : 6 to 255 (8 to 255 for double-density disks)\n");
			usage( *argv);
		}
		if (nbtrk > 256 || nbtrk < 2) {
			printf( "Number of tracks : 2 to 256\n");
			usage( *argv);
		}
		if (ft == 0) {
			if (dd)
				ft = nbsec/2 + 2;
			else
				ft = nbsec;
		} else {
			if (ft < 6 || ft > nbsec) {
				printf( "Track 0 size must > 6 and less than number of sectors (%n)\n", nbsec);
				usage( *argv);
                        }
		}
	}

	ext = strrchr( filename, '.');
	if (ext == NULL) {
		strncat( filename, ".dsk", 5);
	}

	// Don't erase  same name file
	if ((fd = open( filename, O_CREAT | O_WRONLY | O_EXCL, 0664)) < 0) {
		perror( "Operation aborted"); 
		exit( EXIT_FAILURE);
	}

	// print what we are doing
	printf( "Writing Flex image file %s\n", filename);
	printf( "Flex Volume Name '%s' (Vol # %d) ",volname, dsknum);
	printf( "with %d tracks of %d sectors, ", nbtrk, nbsec);
	if (dd)
		printf( "double-density\n");
	else	
		printf( "single-density\n");

	// Now the real part... 
	// First track is special - 2 empty boot sector first
	for (i = 0; i < 256; i++)
		bloc[i] = 0;
	write( fd, bloc, 256);
	write( fd, bloc, 256);

	// Write System Information Record
	for (i = 0; i < 11; i++)
		bloc[i+0x10] = volname[i];			// Volume name
	bloc[0x1B] = (dsknum >> 8) & 0xFF;		// Volume number
	bloc[0x1C] = dsknum & 0xFF;
	bloc[0x1D] = 1;							// First free track
	bloc[0x1E] = 1;							// First free sector
	bloc[0x1F] = nbtrk - 1;					// Last free track
	bloc[0x20] = nbsec;						// Last free sector
	nbfree = (nbtrk - 1) * nbsec;			// Number of free sectors
	bloc[0x21] = (nbfree >> 8) & 0xFF;
	bloc[0x22] = nbfree & 0xFF;
	time( &tloc);
	createdate = localtime( &tloc);
	bloc[0x23] = createdate->tm_mon + 1;	// Date month
	bloc[0x24] = createdate->tm_mday;		// Date day
	bloc[0x25] = createdate->tm_year;		// Date year
	bloc[0x26] = nbtrk - 1;					// End track
	bloc[0x27] = nbsec;						// End sector
	write( fd, bloc, 256);

	// write reserved bloc
	for( i = 0x10; i < 0x28; i++)
		bloc[i] = 0;
	write( fd, bloc, 256);

	// write directory (empty) on first track
	for (i = 5; i < ft; i++) {
		bloc[1] = i + 1;
		write( fd, bloc, 256);
	}
	// last bloc 
	bloc[1] = 0;
	write( fd, bloc, 256);

	// write rest of disk as a free chain from 01/01 to nbtrk/nbsec
	for (i = 1; i < nbtrk; i++) {
		for (j = 1; j < nbsec; j++) {
			bloc[0] = i;
			bloc[1] = j + 1;
			write( fd, bloc, 256);
		}
		if (i < nbtrk-1) {
			bloc[0] = i + 1;
			bloc[1] = 1;
		} else {
			bloc[0] = 0;
			bloc[1] = 0;
		}
		write( fd, bloc, 256);
	}
	close( fd);

	exit(EXIT_SUCCESS);
}
