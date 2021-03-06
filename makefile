# makefile
#
# Copyright (c) 2018 Jonathan Lemos
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.

NAME=ezbackup
VERSION=0.3\ beta
CC=gcc
CXX=g++
CFLAGS=-Wall -Wextra -pedantic -std=c89 -D_XOPEN_SOURCE=500 -DPROG_NAME=\"$(NAME)\" -DPROG_VERSION=\"$(VERSION)\"
CXXFLAGS=-Wall -Wextra -pedantic -std=c++14 -DPROG_NAME=\"$(NAME)\" -DPROG_VERSION=\"$(VERSION)\"
LINKFLAGS=-lssl -lcrypto -lmenu -lncurses -lmega -lstdc++ -ledit -lz -lbz2 -llzma -llz4
DBGFLAGS=-g -Werror
CXXDBGFLAGS=-g -Werror
RELEASEFLAGS=-O3
CXXRELEASEFLAGS=-O3
DIRECTORIES=. cloud compression crypt options strings
# HEADERS=fileiterator maketar crypt readfile error checksum progressbar options checksumsort
HEADERS=$(foreach directory,$(DIRECTORIES),$(shell ls $(directory) | grep .*\\.c$$ | sed 's|\(.*\)\.c$$|$(directory)/\1|g;s|.*main||g'))
CXXHEADERS=$(foreach directory,$(DIRECTORIES),$(shell ls $(directory) | grep .*\\.cpp$$ | sed 's|\(.*\)\.cpp$$|$(directory)/\1|g'))
# TESTS=tests/test_checksum tests/test_crypt tests/test_error tests/test_fileiterator tests/test_maketar tests/test_options tests/test_progressbar
TESTS=$(foreach directory,$(DIRECTORIES),$(shell ls tests/$(directory) | grep .*\\.c$$ | sed 's|\(.*\)\.c$$|tests/$(directory)/\1|g'))
CXXTESTS=$(foreach directory,$(DIRECTORIES),$(shell ls tests/$(directory) | grep .*\\.cpp$$ | sed 's|\(.*\)\.cpp$$|tests/$(directory)/\1|g'))

SOURCEFILES=$(foreach header,$(HEADERS),$(header).c)
OBJECTS=$(foreach header,$(HEADERS),$(header).o)
CXXOBJECTS=$(foreach cxxheader,$(CXXHEADERS),$(cxxheader).cxx.o)
DBGOBJECTS=$(foreach header,$(HEADERS),$(header).dbg.o)
CXXDBGOBJECTS=$(foreach cxxheader,$(CXXHEADERS),$(cxxheader).cxx.dbg.o)
TESTOBJECTS=$(foreach test,$(TESTS),$(test).dbg.o)
TESTCXXOBJECTS=$(foreach cxxtest,$(CXXTESTS),$(cxxtest).cxx.dbg.o)
CLEANOBJECTS=$(foreach header,$(HEADERS),$(header).c.*)
CLEANCXXOBJECTS=$(foreach cxxheader,$(CXXHEADERS),$(header).cpp.*)

release: main.o $(OBJECTS) $(CXXOBJECTS)
	$(CC) -o $(NAME) main.o $(OBJECTS) $(CXXOBJECTS) $(CFLAGS) $(LINKFLAGS) $(RELEASEFLAGS)

debug: main.dbg.o $(DBGOBJECTS) $(CXXDBGOBJECTS)
	$(CC) -o $(NAME) main.dbg.o $(DBGOBJECTS) $(CXXDBGOBJECTS) $(CFLAGS) $(DBGFLAGS) $(LINKFLAGS)

test: $(TESTOBJECTS) $(TESTCXXOBJECTS) $(DBGOBJECTS) $(CXXDBGOBJECTS)
	$(CC) -o tests/test_all $(TESTOBJECTS) $(TESTCXXOBJECTS) $(DBGOBJECTS) $(CXXDBGOBJECTS) $(CFLAGS) $(DBGFLAGS) $(LINKFLAGS)

.PHONY: docs
docs:
	doxygen Doxyfile

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(RELEASEFLAGS)

%.dbg.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(DBGFLAGS)

%.cxx.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(RELEASEFLAGS)

%.cxx.dbg.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(CXXDBGFLAGS)

.PHONY: clean
clean:
	rm -f *.o $(NAME) $(CLEANOBJECTS) $(CLEANCXXOBJECTS) main.c.* vgcore.* $(TESTOBJECTS) $(TESTCXXOBJECTS) tests/*.o cloud/*.o $(DBGOBJECTS) $(OBJECTS)
	rm -rf docs

.PHONY: linecount
linecount:
	wc -l makefile readme.txt $(foreach dir,$(DIRECTORIES),$(dir)/*.h $(dir)/*.c $(dir)/*.cpp) $(foreach test,$(TESTS),$(test).c) $(foreach cxxtest,$(CXXTESTS),$(cxxtest).cpp) tests/test_framework.h tests/test_framework.c tests/test_all.c 2>/dev/null | sort -k2,2

.PHONY: linecount_notests
linecount_notests:
	wc -l makefile readme.txt $(foreach dir,$(DIRECTORIES),$(dir)/*.h $(dir)/*.c $(dir)/*.cpp) 2>/dev/null | sort -k2,2
