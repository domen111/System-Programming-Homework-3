all: server file_reader

server: server.c server-lib.c
	gcc server.c -o server -O2 -Wall -Wextra -Wshadow

file_reader: file_reader.c
	gcc file_reader.c -o file_reader -O2 -Wall -Wextra -Wshadow
