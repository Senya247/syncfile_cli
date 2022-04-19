CC=gcc
CFLAGS=-I.
DEPS = helper.h llist.h
OBJ = helper.o syncfile_cli.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

syncfile_cli: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean :
	-rm *.o $(OB)

