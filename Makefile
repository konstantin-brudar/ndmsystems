all: server

server: server.c
	gcc server.c -o server -Wall

clean:
	rm -f server
