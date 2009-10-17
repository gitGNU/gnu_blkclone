# Makefile for blkclone; block/analyze/fat directory

OBJS=analyze-fat.o

# N.B. !  If Makerules isn't present in or above this directory,
#		then this is an infinite loop.
MKRULE:=$(shell f='Makerules'; while test ! -e $${f}; do f=../$${f}; done; echo $${f})
include ${MKRULE}
