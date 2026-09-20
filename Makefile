# type "make" to build the C++ program, "make test" to run all the tests
CXX = g++
FLAGS = -std=c++17 -O2 -Wall -ffp-contract=off

traffic: cpp/main.cpp cpp/network.h cpp/routing.h cpp/simulation.h
	$(CXX) $(FLAGS) cpp/main.cpp -o traffic

run_tests: cpp/tests.cpp cpp/network.h cpp/routing.h cpp/simulation.h
	$(CXX) $(FLAGS) cpp/tests.cpp -o run_tests

test: traffic run_tests
	./run_tests
	python -m pytest -q

clean:
	rm -f traffic run_tests
