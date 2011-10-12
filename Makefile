CC := gcc
ALLEGRO := /opt/allegro/current
CFLAGS := -I$(ALLEGRO)/include -L$(ALLEGRO)/lib
LDFLAGS := -lallegro -lxml2
SOURCES := $(shell find src/ -type f -name "*.c")
OBJECTS := $(SOURCES:.c=.o)
TARGET := main

all: $(SOURCES) $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "  Linking..."; $(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	@echo "  CC $<"; $(CC) $(CFLAGS) -c -o $@ $< $(LDFLAGS)

clean:
	$(RM) src/*.o $(TARGET)

.PHONY: all clean