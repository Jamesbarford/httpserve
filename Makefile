TARGET := httpserve
CFLAGS := -Wall -Werror -Wextra -O2
CC     := cc

$(TARGET): ./httpserve.c
	$(CC) $(CFLAGS) -o $@ $^

all: $(TARGET)

install:
	install -m0755 $(TARGET) /usr/local/bin
	install -c -m 444 $(TARGET).1 /usr/local/share/man/man1/$(TARGET).1


clean:
	rm $(TARGET)
