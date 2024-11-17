/*****************************************************************************
   mot2cmd - converts Motorola S19 file to .cmd Flex file
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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>

#ifndef NULL
#define NULL 0
#endif

char *errmsg[] = {  "Empty input file", 
                    "Not a Motorola S19 file",
                    "Unexpected char or EOF",
                    "Checksum error",
                    "24 or 32 bits addresses not supported",
                    "Address overflow"
                 };
int verbose = 0;

// Help message

void usage( char *cmd) {
    fprintf( stderr, "Usage: %s [-h] => this help\n", cmd);
    fprintf( stderr, "Usage: %s [-v] <input_file> [<output_file>]\n", cmd);
}

// GetHex : retrieves a hex value in given length from file

int GetHex(FILE *f, int nChars) {
  int out = 0;

  while ( nChars--) {
    int c = getc(f);
    if (c == EOF)
      return -1;
    if (c >= 'a')
      c -= ('a' - 'A');
    if ((!((c >= '0') && (c <= '9'))) &&
        (!((c >= 'A') && (c <= 'F'))))
    return -1;
    c -= '0';
    if (c > 9)
      c -= 7;
    out = out * 16 + c;
  }

  return out;
}

// printbuf : write binary data in CMD file, keep trace of size

int printbuf( FILE *fo, int *buffer, int index) {
  for (int i = 0; i < index; i++)
    fputc( buffer[i], fo);
  if (verbose)
    fprintf( stderr, "write %d bytes at 0x%4X\n", index, (buffer[1] << 8) + buffer[2]);
  return index;
}

// gets19 : read S19 data and write a .CMD Flex file

int gets19( FILE *fi, FILE *fo) {

  int buffer[256], i, j;                 // output buffer
  char nLineType;
  int c = 0;
  int index = 0;
  int nBytes = 0;
  int count = 0;
  int nAddr = 0;
  int checksum;
  int address = 0;

  // clear buffer
  for (i=0; i<256; i++)
      buffer[i]=0;

  while (1) {
    c = getc(fi) ;
    if ((c == '\r') || (c == '\n'))
      continue ;                         // skip newline
    if (c == EOF) {
      if (index == 0)                    // Empty file...
        return 1;
      else {
        nBytes += printbuf( fo, buffer, index);
        for (i=0; i<252-(nBytes%252); i++)
          fputc( 0, fo);
        return 0;
      }
    }
    //printf ("0x%02X(%c) ", c, c);
    if (c != 'S')                        // Starting with 'S' ?
      return 2;                          // No :-(
  
    nLineType = getc( fi);
    if ((count = GetHex(fi,2)) < 3)      // Always between 3 and 255
      return 3;
    if ((nAddr = GetHex(fi,4)) < 0)      // Address between 0 and 0xFFFF
      return 3;
    checksum = count + (nAddr >> 8) + (nAddr & 0xFF);
    //printf( "count = %d, addr = 0x%4X, chksum = 0x%X\n", count, nAddr, checksum);
    count -= 3;

    switch (nLineType)                   // Examine line type
    {
      // Header record, ignore it except for checksum
      // and for printing it if verbose
      case '0' :
        if (verbose)
          fprintf( stderr, "Header: \"");
        while (count--) {
          c = GetHex(fi, 2);
          checksum += c;
          if (verbose)
            fputc( c, stderr);
        }
        if (verbose)
          fprintf( stderr, "\"\n");
        break;
      // Data record (count is always less than 255)
      case '1' :
        if ((index + count > 255) || (address != nAddr)) {
          buffer[3] = index - 4;
          if (index)
            nBytes += printbuf( fo, buffer, index);
          index = 0;
        }

        if (index == 0) {
          buffer[0] = 2;
          buffer[1] = nAddr >> 8;
          buffer[2] = nAddr & 0xFF;
          index = 4;
          address = nAddr;
        } 

        for (i = 0; i < count; i++) {
          if ( ++address > 0xFFFF)
            return 6;
          c = GetHex(fi, 2);             // retrieve a byte
          checksum += c;
          buffer[index++] = c;
        }

        break;
      // 24bit and 32bit data not useful for Flex...
      case '2' :                         // record with 24bit address
      case '3' :                         // record with 32bit address
      case '7' :                         // 32-bit entry point
      case '8' :                         // 24-bit entry point
        return 5;
      // S5/S6 records ignored; don't think they make any sense here
      case '5' :
      case '6' :
        break;
      case '9' :
        buffer[3] = index - 4;
        // nBytes += printbuf( fo, buffer, index);
        // index = 0;
        buffer[index++] = 0x16;
        buffer[index++] = nAddr >> 8;
        buffer[index++] = nAddr & 0xFF;
        if (verbose)
          fprintf( stderr, "Load address = 0x%4X\n", nAddr);
        break;
      default :                          // Type not registered
        return 2;                        // Should not happen
        break;
      }
    c = GetHex(fi, 2);                   // read checksum
    if (((checksum + c) & 0xFF) ^ 0xFF)  // compute checksum
      return 4;
  }
  return 0;
}

// Program start here

int main( int argc, char *argv[]) {
  char *fname = NULL, *pname = NULL;
  int i, opt;
  int status;
  FILE *input;
  FILE *output;

  char outname[13];

  // Read parameters
  while ((opt = getopt( argc, argv, "hv")) != -1) {
    switch (opt) {
    case 'h':
      usage( *argv);
      exit( 0);
      break;
    case 'v':
      verbose = 1;
      break;
    default: /* '?' */
      if (isprint (optopt))
          fprintf( stderr, "Unknown option '-%c'.\n", optopt);
      else
          fprintf( stderr, "Unknown option character '0x%x'.\n", optopt);
      usage( *argv);
      exit( 1);
    }
  }

  if (optind < argc) {
    strncpy( outname, argv[optind], 8);  // limited to 8 chars
    if ((input = fopen( argv[optind], "r")) == NULL) {
      perror( argv[optind]);
      usage( *argv);
      exit( 2);
    }
    optind++;
  } else {
    fprintf( stderr, "No input file...\n");
    usage( *argv);
    exit( 1);
  }

  // Output file name
  if (optind < argc) {
    pname = argv[optind];
    if ((output = fopen( pname, "w")) == NULL) {
      perror( pname);
      usage( *argv);
      exit(2);
    }
    if (verbose)
      fprintf( stderr, "Writing to %s\n", pname);
  } else {
    for (i = 0; i < 8; i++)
      if (outname[i] == '.' || outname[i] == 0)
        break;
    outname[i++] = '.';
    outname[i++] = 'C';
    outname[i++] = 'M';
    outname[i++] = 'D';
    outname[i++] = 0;
    if ((output = fopen( outname, "w")) == NULL) {
      perror( outname);
      usage( *argv);
      exit(2);
    }
    if (verbose)
      fprintf( stderr, "Writing to %s\n", outname);
  }

  // All is done here
  status = gets19( input, output);

  if (status)
    fprintf( stderr, "Error: %s\n", errmsg[status-1]);

  if (input)
    fclose(input);
  if (output)
    fclose(output);
 
  exit (status);
}
