CXXFLAGS := --std=c++11 -Wall
LINKER_FLAGS = 
EXECUTABLE = EggArchiveBuilder

SOURCES = main.cpp lz4.c lz4hc.c

$(EXECUTABLE):
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@
	
clean:
	rm $(EXECUTABLE)
