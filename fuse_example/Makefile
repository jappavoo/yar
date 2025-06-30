.PHONY: clean

fuse_epoll: fuse_epoll.c
	gcc -g -Wall -o fuse_epoll fuse_epoll.c $(shell pkg-config fuse3 --cflags --libs)

clean:
	rm -rf $(wildcard fuse_epoll)
