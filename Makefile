editor: editor.o
	gcc editor.o -o editor

editor.o: editor.c
	gcc editor.c -o editor.o -c
