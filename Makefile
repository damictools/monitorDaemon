CPP = g++
CC = gcc
CFLAGS = -Wall
LIBS = 
GLIBS = 
GLIBS += 
OBJECTS = monidae.o 
HEADERS = 

ALL : monidae.exe
	@echo "Listo!"

monidae.exe : $(OBJECTS)
	$(CPP) $(OBJECTS) -o monidae.exe $(LIBS) $(GLIBS) $(CFLAGS)

monidae.o : monidae.cc $(HEADERS)
	$(CPP) -c monidae.cc -o monidae.o $(CFLAGS)

clean:
	rm -f *~ *.o *.exe
