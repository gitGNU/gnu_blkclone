# Makefile for blkclone; block directory

SUBDIRS=analyze sparsecopy

OBJS=map-parse-v1.o

# N.B. !  If Makerules isn't present in or above this directory,
#		then this is an infinite loop.
MKRULE:=$(shell f='Makerules'; while test ! -e $${f}; do f=../$${f}; done; echo $${f})
include ${MKRULE}
