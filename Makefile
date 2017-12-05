CC = mpicc
CFLAGS = -O -I. -I/opt/local/include
DEPS = time_util.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: pingpong

pingpong: pingpong.o
	$(CC) -o $@ $^ $(CFLAGS) -L/opt/local/lib -largp

.PHONY: clean

clean:
	rm -f *.o *~ core
