CFLAGS=-Wall

all: clean discont

discont:
	$(CC) $(CFLAGS) -o discont discont.c rtp.cpp bufdata.cpp  nlog.cpp -lpthread -lconfig 
#-DNDEBUG

.PHONY: clean

clean:
	$(RM) discont
