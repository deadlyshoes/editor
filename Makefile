kilo: kilo.o
	gcc kilo.o -o kilo

kilo.o: kilo.c
	gcc kilo.c -o kilo.o -c
