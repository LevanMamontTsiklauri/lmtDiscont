CFLAGS=-Wall

all: clean discont

discont:
	$(CC) $(CFLAGS) -o discont discont.c  -lpthread -lconfig 
#-DNDEBUG rtp.cpp bufdata.cpp  nlog.cpp

.PHONY: clean

clean:
	$(RM) discont
