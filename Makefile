all: program.cpp
	g++ -std=c++17 program.cpp -o program
clean:
	rm program