IDIR = include
ODIR = build
CDIR = src
BDIR = bin
CC = g++
#CFLAGS = -I$(IDIR)
CFLAGS = $(patsubst %,-I%,$(CDIRS))
LIBS = -levent 

_CDIRS = core net
CDIRS = $(patsubst %,$(CDIR)/%,$(_CDIRS))

vpath %.h $(CDIRS)
vpath %.cpp $(CDIRS)

_OBJ = Main.o NetUtils.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

_DEPS = $(_OBJ:.o=.h)
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

#$(ODIR)/%.o: %.c $(DEPS)
$(ODIR)/%.o: %.cpp $(_DEPS)
	$(CC) -g -c -o $@ $< $(CFLAGS)

#$(IDIR)/%: %
#	cp $^ $(IDIR)/.

#all: penn_daq tut $(DEPS)
all: directories penn_daq

directories:
	test -d $(ODIR) || mkdir $(ODIR) 
	test -d $(BDIR) || mkdir $(BDIR) 
	test -d logs || mkdir logs 
	test -d macro || mkdir macro 

penn_daq: $(OBJ)
	$(CC) -g -o $(BDIR)/$@ $^ $(CFLAGS) $(LIBS) 

tut:
	python $(CDIR)/tut/tut_gen.py
	$(CC) -lreadline -lncurses -o $(BDIR)/tut $(CDIR)/tut/tut.c $(CFLAGS)
    
clean: 
	rm -f $(ODIR)/* $(BDIR)/*
#rm -f $(ODIR)/*.o core $(IDIR)/* $(BDIR)/*