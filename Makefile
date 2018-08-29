CXXFLAGS=-Wall -O3 -std=c++11

dcf77sim: dcf77sim.o dcf77-source.o gpio.o
	$(CXX) -o $@ $^

clean:
	rm -f *.o dcf77sim
