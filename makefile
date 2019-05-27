CXX = g++

#static library: sqlite3.o
SQLITEDIR = external/SQLite
SQLITELIB = sqlite3.o
SQLITESRC = sqlite3.c

srcdir = src
objdestdir = build
libdestdir = lib
includedir = include

SOURCES = dal.cpp
OBJECTS = $(SOURCES:%.cpp=%.o)
objects = $(OBJECTS:%=$(objdestdir)/%)
OUT_FILE = libdal.a

vpath %.cpp $(srcdir)
vpath %.h $(includedir)
vpath %.o $(objdestdir)
vpath %.a $(libdestdir)


# Order-only prerequisites can be specified by placing a pipe symbol (|) in the prerequisites list: any prerequisites to the left of the pipe symbol are normal; any prerequisites to the right are order-only
# we want the libdestdir to be created before any targets are placed into it but, because the timestamps on directories change whenever a file is added, removed, or renamed, we certainly don’t want to rebuild all the targets
$(libdestdir)/$(OUT_FILE): $(OBJECTS) $(SQLITEDIR)/$(SQLITELIB) | $(libdestdir)
	ar -cvr $(libdestdir)/$(OUT_FILE) $(objects) $(SQLITEDIR)/$(SQLITELIB)


debug: $(libdestdir)/$(OUT_FILE)
ndebug: $(libdestdir)/$(OUT_FILE)
dbinit: debug


#target specific variable (in effect for all prerequisites of this target, and all their prerequisites)
debug: CXXFLAGS += -g
ndebug: CXXFLAGS += -DNDEBUG
dbinit: CXXFLAGS += -D DB_INIT
dal.o: CXXFLAGS += -I$(SQLITEDIR) -I$(includedir)


# symbol $< is the name of the first prerequisite
# symbol $@ is the file name of the target of the rule. If the target is an archive member, then ‘$@’ is the name of the archive file
# symbol | impose a specific ordering on the rules to be invoked without forcing the target to be updated if one of those rules is executed
# since this is a pattern rule it's just like an implicit rule - it's subsidiary not obligatory thus if there is no %.cpp file this rule is not applicable
%.o: %.cpp | $(objdestdir)
	$(CXX) $(CXXFLAGS) -c -std=c++14 -Wall -Wextra -pedantic $< -o $(objdestdir)/$@


$(objdestdir) $(libdestdir) $(includedir):
	mkdir -p $@


dal.o: dal.h

$(SQLITEDIR)/$(SQLITELIB) :
	cc -c $(SQLITEDIR)/$(SQLITESRC) -o $(SQLITEDIR)/$(SQLITELIB)


.PHONY: clean
clean:
	find . \( -name "$(OUT_FILE)" -or -name "*.o" \) -delete
	rm -f $(SQLITEDIR)/$(SQLITELIB)
