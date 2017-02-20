program = silo
objs = main.o global.o
srcs = $(objs:%.o=%.c)
CC = icpc # g++
CFLAGS = -std=c++11 -Wall -qopenmp -mmic -O2 -lpthread # -g -lprofiler

all: silo

.SUFFIXES: .cpp .o

.cpp.o:
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(objs) *.d *~ *.exe

silo: $(objs)
	$(CC) $(CFLAGS) -o $(program).exe $^
