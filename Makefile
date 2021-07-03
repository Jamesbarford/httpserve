TARGET := httpserve
CFLAGS := -Wall -Werror -Wextra -O2
CC     := cc

PREFIX?=/usr/local

$(TARGET): ./httpserve.c
	$(CC) $(CFLAGS) -o $@ $^

all: $(TARGET)

install:
	mkdir -p $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -c -m 555 $(TARGET) $(PREFIX)/bin
	install -c -m 444 $(TARGET).1 $(PREFIX)/share/man/man1/$(TARGET).1


clean:
	rm $(TARGET)
