CXXFLAGS=-Wall -O3 -std=c++11
TIME_SOURCES=dcf77-source.o wwvb-source.o jjy-source.o msf-source.o

txtempus: txtempus.o  gpio.o $(TIME_SOURCES)
	$(CXX) -o $@ $^

clean:
	rm -f *.o txtempus
