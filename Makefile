CFLAGS = -Wall
CFLAGS += $(shell sdl2-config --cflags) 
CFLAGS += $(shell pkg-config --cflags json-c)

LIBS += $(shell pkg-config --libs json-c SDL2_ttf libmurmurhash)

PROG = offblast

${PROG}: main.o
	gcc -o ${PROG} main.o ${LIBS} 

main.o: main.c
	gcc -c ${CFLAGS} main.c

clean:
	rm -f ./*.o
	rm -f ${PROG}

