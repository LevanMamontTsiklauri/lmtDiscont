CFLAGS=-Wall -I /usr/local/include -L/usr/local/lib/

all: clean discont

discont:
	$(CC) $(CFLAGS) -o discont discont.c  -lpthread -lconfig 

.PHONY: clean

clean:
	$(RM) discont
