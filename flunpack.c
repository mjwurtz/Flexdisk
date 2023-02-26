/* flunpack.c -- Flex text file uncompressor 
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

int main ( int argc, char *argv[]) {

	FILE *input, *output;
	int chin;		// readed char
	int flag = 0;	// space compression ?
	int nspace;		// number of spaces

	if (argc > 1) {
		if ((input = fopen( argv[1], "r")) == NULL) {
			perror( argv[1]);
			fprintf( stderr, "Usage: %s [input_file]\n", argv[0]);
			exit( 1);
		}
	} else
		input = stdin;
	if (argc > 2) {
		if ((output = fopen( argv[2], "w")) == NULL) {
			perror( argv[2]);
			fprintf( stderr, "Usage: %s [input_file]\n", argv[0]);
			exit( 1);
		}
	} else
		output = stdout;

	while ((chin = fgetc( input)) != EOF) {
		if (flag == 1) {
			nspace = chin;			// number of spaces follows TAB
			while (nspace--)
				fputc( ' ', output);
			flag = 0;
			continue;
		}
		if (chin == '\t')			// TAB => prepare for multiple spaces
			flag = 1;
		else if (chin == '\r')
			fputc( '\n', output);	// CR to LF conversion
		else if (chin != 0)
			fputc( chin, output);	// do nothing for null in file
	}
	exit( 0);
}
