CFLAGS=-c -Wall -O3 -Wno-unused-result
# CFLAGS=-c -Wall -g -Wno-unused-result

urubasic: main.o urubasic.o smemblk.o
	gcc -o $@ $^

main.o: main.c urubasic.h stdintw.h
urubasic.o: urubasic.c urubasic.h stdintw.h smemblk.h
smemblk.o: smemblk.c stdintw.h smemblk.h

%.o: %.c
	gcc $(CFLAGS) $<

clean:
	rm -f urubasic.o main.o urubasic

all: clean urubasic

.PHONY: test
test: urubasic
	@./runtests.sh

.PHONY: cleantest
cleantest:
	@find -regextype posix-extended -regex '.*\.(res|ok|pc|out)' -type f -delete
