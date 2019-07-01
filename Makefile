CFLAGS = -Wall
CFLAGS += $(shell sdl2-config --cflags) 
CFLAGS += $(shell pkg-config --cflags json-c libcurl gumbo)

LIBS += $(shell pkg-config --libs json-c SDL2_ttf libmurmurhash libcurl gumbo)

PROG = offblast
OBJS = main.o offblastDbFile.o

${PROG}: ${OBJS}
	gcc -g -o ${PROG} ${OBJS} ${LIBS} 

main.o: main.c offblast.h offblastDbFile.h
	gcc -g -c ${CFLAGS} main.c

offblastDbFile.o: offblastDbFile.c offblast.h offblastDbFile.h
	gcc -g -c  offblastDbFile.c

clean:
	rm -f ./*.o
	rm -f ${PROG}

