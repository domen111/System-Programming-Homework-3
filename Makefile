args = -Wall -Wextra -Wshadow -O2 #-g -fsanitize=address -fsanitize=undefined

all: server file_reader

server: server.c server-lib.c
	gcc server.c -o server $(args)

file_reader: file_reader.c
	gcc file_reader.c -o file_reader $(args)
