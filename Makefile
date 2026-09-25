CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lraylib -lm -lpthread -ldl -lcurl -lcjson

TARGET = console_ui
SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET) test_parse test_cache_dir test_data_dir test_notifications

test: test_parse test_cache_dir test_data_dir test_notifications
	./test_parse
	./test_cache_dir
	./test_data_dir
	./test_notifications

test_parse: test_parse.c
	$(CC) $(CFLAGS) -o test_parse test_parse.c $(LDFLAGS)

test_cache_dir: test_cache_dir.c
	$(CC) $(CFLAGS) -o test_cache_dir test_cache_dir.c $(LDFLAGS)

test_data_dir: test_data_dir.c
	$(CC) $(CFLAGS) -o test_data_dir test_data_dir.c $(LDFLAGS)

test_notifications: test_notifications.c
	$(CC) $(CFLAGS) -o test_notifications test_notifications.c $(LDFLAGS)
