# Makefile for blkclone; block/analyze directory

SUBDIRS=fat ntfs

OBJS=dispatch.o

# N.B. !  If Makerules isn't present in or above this directory,
#		then this is an infinite loop.
MKRULE:=$(shell f='Makerules'; while test ! -e $${f}; do f=../$${f}; done; echo $${f})
include ${MKRULE}
