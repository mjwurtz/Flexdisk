/* flpack.c -- Unix to Flex text file compressor 
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

#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <ctype.h>

int main ( int argc, char *argv[]) {

	FILE *input, *output;
	int chin;			// readed char
	int tabstop;		// tab stop value
	int line_length;	// current line length
	int nspace;			// number of spaces
	int opt;			// opt value

	while ((opt = getopt( argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			sscanf( optarg, "%d", &tabstop);
			break;
		default:
			if (optopt == 't')
				fprintf( stderr, "Option %c requires an argument.\n", optopt);
        	else if (isprint (optopt))
          		fprintf( stderr, "Unknown option '-%c'.\n", optopt);
      		else
         		fprintf( stderr, "Unknown option character '0x%x'.\n", optopt);
			fprintf( stderr, "Usage: flpack [-t value] [input [output]]\n");
			exit( 1);
		}
	}

		if (optind < argc) {
			if ((input = fopen( argv[optind], "r")) == NULL) {
				perror( argv[optind]);
				fprintf( stderr, "Usage: flpack [-t value] [input [output]]\n");
				exit( 1);
			}
			optind++;
		} else
			input = stdin;
		if (optind < argc) {
			if ((output = fopen( argv[optind], "w")) == NULL) {
				perror( argv[optind]);
				fprintf( stderr, "Usage: flpack [-t value] [input [output]]\n");
				exit( 1);
			}
		} else
			output = stdout;

	line_length = 0;
	nspace = 0;
	tabstop = 4;
	while ((chin = fgetc( input)) != EOF) {
		if (chin == ' ') {
			nspace++;
			line_length++;
		} else if (chin == '\t') {	// TAB => prepare for multiple spaces
			nspace += tabstop - (line_length % tabstop);
		} else if (chin == '\n') {
			nspace = 0;				// throw away trailing spaces
			line_length = 0;		// Line length reinit
			fputc( '\r', output);	// CR to LF conversion
		} else {
			switch (nspace) {		// time to output them
			case 2:
			  fputc( ' ', output);
			case 1:
			  fputc( ' ', output);
			case 0:
			  break;
			default:
			  fputc( '\t', output);
			  fputc( nspace & 0x7f, output);		// won't work if nspace > 127 !!!
			  break;
			}
			nspace = 0;
			fputc( chin, output);
			line_length++;
		}
	}
	exit( 0);
}
