Refer to COL216_Assignment5.pdf for details
In program.cpp, setting OPTIMIZED to true will activate non-blocking memory, setting VERBROSE to true will give cycle wise output. 
Compile the program with `make`
Run the program with `./program <folder_name> N M ROW_ACCESS_DELAY COLUMN_ACCESS_DELAY`. Row and column delays are by default 10 and 2.
Example: `./program ./tests/test1/ 1 50 > output`
Remove the executable with `make clean`
Note: Since the output is large, it is recommended directing it to a file.