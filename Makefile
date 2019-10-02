################################################################################
#  File           : Makefile
#  Description    : This is the Makefile for block_sim.
#
#  Author         : Patrick McDaniel
#  Last Modified  : Mon Jul 8 00:00:00 EDT 2019
#

# Locations
CMPSC311_LIBDIR=.

# Make environment
INCLUDES=-I. -I$(CMPSC311_LIBDIR)
CC=gcc
CFLAGS=-I. -c -g -Wall $(INCLUDES)
LINKARGS=-g
LIBS=-lblocklib -lcmpsc311 -lgcrypt -lcurl -L$(CMPSC311_LIBDIR) 
                    
# Suffix rules
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS)  -o $@ $<
	
# Files
OBJECT_FILES=	block_sim.o \
				block_driver.o \
				block_cache.o
				
# Productions
all : block_sim

block_sim : $(OBJECT_FILES)
	$(CC) $(LINKARGS) $(OBJECT_FILES) -o $@ $(LIBS)

clean : 
	rm -f block_sim $(OBJECT_FILES) block_memsys.bck
