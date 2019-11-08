CFLAGS = -Wall
CFLAGS += $(shell sdl2-config --cflags) 
CFLAGS += $(shell pkg-config --cflags json-c libcurl gl glew)
CFLAGS += -pthread

LIBS += $(shell sdl2-config --libs) 
LIBS += $(shell pkg-config --libs json-c gl glew libmurmurhash libcurl) -pthread

PROG = offblast
OBJS = main.o offblastDbFile.o

#TODO Optimization on for production!

${PROG}: ${OBJS}
	gcc -g -o ${PROG} ${OBJS} -lm ${LIBS} 

main.o: main.c offblast.h offblastDbFile.h shaders/*
	gcc -g -c ${CFLAGS} main.c

offblastDbFile.o: offblastDbFile.c offblast.h offblastDbFile.h
	gcc -g -c  offblastDbFile.c

clean:
	rm -f ./*.o
	rm -f ${PROG}

install:
	mkdir -p ~/.offblast
	cp -i config-dist.json ~/.offblast/config.json

