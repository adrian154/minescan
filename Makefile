OBJS := bin/main.o bin/addr-gen.o bin/sqlite3/sqlite3.o

bin/minescan: $(OBJS)
	gcc $^ -o $@ -g

bin/sqlite3/sqlite3.o: sqlite/sqlite3.c
	mkdir -p bin/sqlite3
	gcc $< -c -o $@ -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -O2 -g

bin/%.o: %.c
	mkdir -p bin
	gcc $< -c -o $@ -Wall -Wextra -Wpedantic -std=c11 -O2 -g