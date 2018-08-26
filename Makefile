CXXFLAGS=-Wall -O3

dcf77sim: dcf77sim.o gpio.o
	$(CXX) -o $@ $^

clean:
	rm -f *.o dcf77sim
