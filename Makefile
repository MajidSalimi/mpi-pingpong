CC = mpicc
CFLAGS = -O -std=gnu99 -I. -I/opt/local/include
LDFLAGS = -L/opt/local/lib
LDLIBS =
DEPS = time_util.h

UNAME_S = $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  LDLIBS += -largp
endif

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: pingpong

pingpong: pingpong.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LDLIBS)

.PHONY: clean

clean:
	rm -f *.o *~ core
