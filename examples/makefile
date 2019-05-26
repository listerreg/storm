CXX = g++

#static library: libdal.a
DALDIR = ..
DALLIBDIR = ../lib
DALLIB = libdal.a
DALINCLUDEDIR = ../include
DALHEADER = dal.h
DALLIBFLAG = -ldal

SQLITEFLAGS = -ldl -pthread

CXXLINKFLAGS = -L$(DALLIBDIR) $(DALLIBFLAG) $(SQLITEFLAGS)

SOURCES = main.cpp
OBJECTS = $(SOURCES:%.cpp=%.o)
OUT_FILE = runme

$(OUT_FILE): $(OBJECTS) $(DALLIBDIR)/$(DALLIB)
	$(CXX) -o $(OUT_FILE) $(OBJECTS) $(CXXLINKFLAGS)

%.o: %.cpp
#	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c -Wall -pedantic -std=c++0x $<
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c -std=c++0x $<

debug: $(OUT_FILE)
ndebug: $(OUT_FILE)
dbinit: $(OUT_FILE)

#### target specific variable (in effect for all prerequisites of this target, and all their prerequisites) ####
debug: CXXFLAGS += -g
debug: SUBMAKEFLAGS += debug
ndebug: CXXFLAGS += -DNDEBUG
ndebug: SUBMAKEFLAGS += ndebug
dbinit: CXXFLAGS += -g -D DB_INIT
dbinit: SUBMAKEFLAGS += dbinit
main.o: CXXFLAGS += -I$(DALINCLUDEDIR)

$(DALLIBDIR)/$(DALLIB):
	$(MAKE) -C $(DALDIR) $(SUBMAKEFLAGS)

#recipes are executed by invoking a new sub-shell for each line of the recipe
#use the shell AND operator (&&) so that if the cd (not this case) command fails, the script will fail without trying to invoke another command in the wrong directory
.PHONY: clean
clean:
	rm -f $(OUT_FILE) && rm -f *.o
	$(MAKE) -C $(DALDIR) $@
