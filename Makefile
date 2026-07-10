# Super simple makefile for lazy programmer...

MAN = /usr/local/man/man1
BIN = ~/bin
CC  = gcc
LDFLAGS =

all: flan fldump flfmt flpack flunpack flwrite mot2cmd

.c.o:
	$(CC) -c $@ $<

flfmt: flfmt.c
	$(CC) -o flfmt flfmt.c
flan: flan.o tstflex.o dskflex.h
	$(CC) $(LDFLAGS) -o flan flan.o tstflex.o
fldump: fldump.o tstflex.o dskflex.h
	$(CC) -o fldump fldump.o tstflex.o
flread: flread.o tstflex.o dskflex.h
	$(CC) $(LDFLAGS) -o flread flread.o tstflex.o
flwrite: flwrite.o tstflex.o dskflex.h
	$(CC) $(LDFLAGS) -o flwrite flwrite.o tstflex.o
flpack: flpack.c
	$(CC) -o flpack flpack.c
flunpack: flunpack.c
	$(CC) -o flunpack flunpack.c
mot2cmd: mot2cmd.c
	$(CC) -o mot2cmd mot2cmd.c

install: all
	mkdir -p $(BIN)
	cp flan fldump flfmt flpack flunpack flwrite mot2cmd $(BIN)
	ln -f $(BIN)/flwrite $(BIN)/fldel

man: flan.1 fldump.1 flfmt.1 flpack.1 flunpack.1 flwrite.1 mot2cmd.1
	cp flan.1 fldump.1 flfmt.1 flpack.1 flunpack.1 flwrite.1 mot2cmd.1 $(MAN)

clean:
	rm -f flan fldump flfmt flpack flunpack flwrite mot2cmd

