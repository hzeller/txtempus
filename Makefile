CXXFLAGS=-Wall -O3 -std=c++11

txtempus: txtempus.o dcf77-source.o wwvb-source.o gpio.o
	$(CXX) -o $@ $^

clean:
	rm -f *.o txtempus
