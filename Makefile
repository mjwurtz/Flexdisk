# Super simple makefile for lazy programmer...

MAN = /usr/local/man/man1
BIN = ~/bin

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

all: flan fldump fflex flpack flunpack flwrite

install: all
	mkdir -p $(BIN)
	cp flan fldump fflex flpack flunpack flwrite $(BIN)
	ln -f $(BIN)/flwrite $(BIN)/fldel

man: flan.1 fldump.1 fflex.1 flpack.1 flunpack.1 flwrite
	cp flan.1 fldump.1 fflex.1 flpack.1 flunpack.1 flwrite.1 $(MAN)

clean:
	rm -f flan fldump fflex flpack flunpack flwrite

