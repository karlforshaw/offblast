CFLAGS = -Wall
CFLAGS += $(shell sdl2-config --cflags) 
CFLAGS += $(shell pkg-config --cflags json-c)

LIBS = $(shell pkg-config --libs json-c)
LIBS += $(shell sdl2-config --libs) -lSDL2_ttf 

PROG = offblast

${PROG}: main.o
	gcc -o ${PROG} main.o ${LIBS} 

main.o: main.c
	gcc -c ${CFLAGS} main.c

clean:
	rm -f ./*.o
	rm -f ${PROG}

