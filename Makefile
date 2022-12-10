CC = gcc
CFLAGS = -fPIC -Wall -Wextra

.PHONY: build
build: prepare

prepare: libscheduler.so
	sudo cp $^ /lib

libscheduler.so: so_scheduler.o
	$(CC) -shared -o $@ $^

so_scheduler.o: so_scheduler.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	-rm -f so_scheduler.o libscheduler.so 
