CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -pthread
SRCS     := serve.cpp session.cpp
BINS     := $(SRCS:.cpp=)

all: $(BINS)

%: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BINS) frame*.jpg

.PHONY: all clean
