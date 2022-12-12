OBJS := bin/main.o bin/addr-gen.o

bin/minescan: $(OBJS) bin/sqlite3.o
	gcc $^ -o $@ -g

bin/sqlite3/sqlite3.o: sqlite/sqlite3.c
	gcc $< -c -o $@ -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -O2 -g

bin/%.o: %.c
	gcc $< -c -o $@ -Wall -Wextra -Wpedantic -std=c11 -O2 -g