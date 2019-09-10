
lib/libmpisessions.so: obj/kvs.o obj/sessions.o
	mpicc -shared -fPIC -o lib/libmpisessions.so obj/kvs.o obj/sessions.o

obj/kvs.o: src/kvs.c
	mkdir obj
	mpicc -fPIC -I include/ -c src/kvs.c -o obj/kvs.o

obj/sessions.o: src/sessions.c
	mpicc -fPIC -I include/ -c src/sessions.c -o obj/sessions.o

.PHONY: clean

clean:
	rm -rf obj
	rm -rf lib
