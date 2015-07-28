all: sqlsmith

OBJECTS = relmodel.o schema.o random.o expr.o grammar.o log.o dump.o sqlsmith.o

CXXFLAGS = -O3 -g

CXX = g++
LDFLAGS = -lpqxx -lpq -lstdc++ -lpthread

# clang compiles sqlsmith faster, but produces slightly slower code
# 225 vs 250 queries/s

# CXX = clang++
# LDFLAGS = -lpqxx -lpq -lc++ -lpthread

config.h: .git/*
	echo "#define GITREV \"$$(git describe --dirty --tags --always)\"" > $@

sqlsmith: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -std=c++11 -Wall $+ $(LDFLAGS) -o $@

%.o: %.cc config.h *.hh
	$(CXX) $(CXXFLAGS) -std=c++11 -Wall -c $< -o $@

clean:
	rm -f sqlsmith *.o config.h *.xml

%.v: %.xml
	xsltproc dump.xsl $< | dot -Tx11
