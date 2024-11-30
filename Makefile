# Super simple makefile for lazy programmer...

MAN = /usr/local/man/man1
BIN = ~/bin

all: flan fldump fflex flpack flunpack flwrite mot2cmd

flan: flan.c dsk.h
	cc -o flan flan.c
fldump: fldump.c dsk.h
	cc -o fldump fldump.c
fflex: fflex.c
	cc -o fflex fflex.c
flpack: flpack.c dsk.h
	cc -o flpack flpack.c
flunpack: flunpack.c dsk.h
	cc -o flunpack flunpack.c
flwrite: flwrite.c dsk.h
	cc -o flwrite flwrite.c
mot2cmd: mot2cmd.c
	cc -o mot2cmd mot2cmd.c

install: all
	mkdir -p $(BIN)
	cp flan fldump fflex flpack flunpack flwrite mot2cmd $(BIN)
	ln -f $(BIN)/flwrite $(BIN)/fldel

man: flan.1 fldump.1 fflex.1 flpack.1 flunpack.1 flwrite.1 mot2cmd.1
	cp flan.1 fldump.1 fflex.1 flpack.1 flunpack.1 flwrite.1 mot2cmd.1 $(MAN)

clean:
	rm -f flan fldump fflex flpack flunpack flwrite mot2cmd

