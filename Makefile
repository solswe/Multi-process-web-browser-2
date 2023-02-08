CC=gcc
CFLAGS= -g -Wall `pkg-config --cflags gtk+-3.0 webkit2gtk-4.0`
LDFLAGS+=`pkg-config --libs gtk+-3.0 webkit2gtk-4.0`
DEPS = -Wno-deprecated -Wno-deprecated-declarations 

LIB=/usr/lib/
BINS=wrapper.o util.o
EXS=browser 

all:  $(EXS) $(BINS)

browser: $(BINS) browser.c
	$(CC) $(BINS) browser.c $(DEPS) -L ${LIB} $(CFLAGS) $(LDFLAGS) -o browser

submission: clean
	@echo "Creating Tar Submission..."
	@read -p "Please enter your group number: " grou_num; \
	tar_name="group_"$$grou_num"_p1_submission.tar.gz";\
	rm -f $$tar_name;tar -czvf $$tar_name browser.c Makefile render wrapper.o util.o wrapper.h util.h .blacklist .favorites;\
	echo "Submission Created: " $$tar_name

clean:
	rm -rf $(EXS)


