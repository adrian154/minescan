bin/minescan: bin/minescan.o bin/sqlite3.o
	gcc $^ -o $@

bin/sqlite3.o: sqlite/sqlite3.c
	gcc $< -c -o $@ -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -O2

bin/minescan.o: main.c
	gcc $< -c -o $@ -Wall -Wextra -Wpedantic -std=c11 -O2
