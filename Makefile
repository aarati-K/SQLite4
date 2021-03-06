#-------------------------------------------------------------------------------
# Project Specific Parameters
#-------------------------------------------------------------------------------

include Make.defines
include Make.opts

#-------------------------------------------------------------------------------
# Directories
#-------------------------------------------------------------------------------

PREFIX      = /usr/local
DESTDIR     = 
BINDIR      = $(DESTDIR)/$(PREFIX)/bin
INCDIR      = $(DESTDIR)/$(PREFIX)/include
LIBDIR      = $(DESTDIR)/$(PREFIX)/lib
INFODIR     = $(DESTDIR)/$(PREFIX)/info
MANDIR      = $(DESTDIR)/$(PREFIX)/man
TOP_SRCDIR  = .
SRCDIR      = .

include Make.linux

#-------------------------------------------------------------------------------
# Compile Options
#-------------------------------------------------------------------------------

CPP         = g++
CC          = gcc
CFLAGS      = $(ADDLCFLAGS) $(INCPATHS) \
              $(PROFILING) $(MEM_TRACING) $(DEBUGGING) $(TRACING)
CPPFLAGS    =  $(ADDCPPFLAGS)
LDFLAGS     =   $(ADDLDFLAGS)
LIBS        =      $(ADDLIBS)
INSTALL     = /usr/bin/install -c

#-------------------------------------------------------------------------------
# Targets
#-------------------------------------------------------------------------------

LIBVER      = .0.0
A_LIB       = lib$(NAME).a
S_LIB       = lib$(NAME).$(DSO_EXTENSION)
VS_LIB      = $(S_LIB).$(LIBVER)
PROGS       = all
FILES       = 
LIBFILES    = lib.o fs.o common.o
HDR         =
CLEANFILES  = gmon.out prof.txt *core		      \
	          *.o *~ *.$(DSO_EXTENSION) *.a 

BUILDFILES  = config.h autoscan.log config.status \
              tmp.txt configure config.log configure.scan

#-------------------------------------------------------------------------------
# Autoconf
#-------------------------------------------------------------------------------

# automatic re-running of configure if the ocnfigure.in file has changed
${SRCDIR}/configure: configure.in
	cd ${SRCDIR} && autoconf

# autoheader might not change config.h.in, so touch a stamp file
${SRCDIR}/config.h.in: stamp-h.in
${SRCDIR}/stamp-h.in: configure.in aclocal.m4
		cd ${SRCDIR} && autoheader
		echo timestamp > ${SRCDIR}/stamp-h.in

config.h: stamp-h

stamp-h: config.h.in config.status
	./config.status

Makefile: Makefile.in config.status
	./config.status

config.status: configure
	./config.status --recheck
