CXXFLAGS=-Wall -O3 -std=c++11
PREFIX?=/usr

TIME_SOURCES=dcf77-source.o wwvb-source.o jjy-source.o msf-source.o

txtempus: txtempus.o  gpio.o $(TIME_SOURCES)
	$(CXX) -o $@ $^

install: txtempus
	install txtempus $(PREFIX)/bin

clean:
	rm -f *.o txtempus
