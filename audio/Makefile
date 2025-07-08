# Makefile for Linux Audio Circular Buffer Application
# For BeagleY-AI AM67A platform

# Compiler and tools
CC = gcc
STRIP = strip

# Target application
TARGET = audio_circular_buffer

# Source files
SOURCES = audio_circular_buffer.c shared_memory.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Include directories
INCLUDES = -I. \
          -I/usr/include \
          -I/usr/local/include

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -g \
         -D_GNU_SOURCE \
         -std=c99 \
         $(INCLUDES)

# Linker flags
LDFLAGS = -lm -lpthread

# Try to use TI RPMsg library if available
ifeq ($(shell pkg-config --exists ti_rpmsg_char 2>/dev/null && echo yes),yes)
    CFLAGS += -DHAVE_TI_RPMSG=1
    LDFLAGS += $(shell pkg-config --libs ti_rpmsg_char)
    INCLUDES += $(shell pkg-config --cflags ti_rpmsg_char)
else
    $(info Note: TI RPMsg library not found, using standard Linux RPMsg)
endif

# Build rules
.PHONY: all clean install uninstall debug release

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build
debug: CFLAGS += -DDEBUG -g3 -O0
debug: $(TARGET)

# Release build
release: CFLAGS += -DNDEBUG -O3
release: $(TARGET)
	$(STRIP) $(TARGET)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET)

# Install to system
install: $(TARGET)
	@echo "Installing $(TARGET)..."
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod 755 /usr/local/bin/$(TARGET)
	@echo "Installation complete"

# Uninstall from system
uninstall:
	@echo "Uninstalling $(TARGET)..."
	sudo rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstallation complete"

# Setup development environment
setup:
	@echo "Setting up development environment..."
	@echo "Checking for required packages..."
	@if ! dpkg -l | grep -q ti-rpmsg-char; then \
		echo "Error: ti-rpmsg-char package not found"; \
		echo "Please install TI RPMsg development packages"; \
		exit 1; \
	fi
	@echo "Environment setup complete"

# Run the application (requires root for /dev/mem access)
run: $(TARGET)
	@echo "Running $(TARGET) (requires root privileges)..."
	sudo ./$(TARGET)

# Create tarball for distribution
dist: clean
	@echo "Creating distribution tarball..."
	tar -czf audio_circular_buffer.tar.gz \
		*.c *.h Makefile README.md
	@echo "Distribution tarball created: audio_circular_buffer.tar.gz"

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the application (default)"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build optimized release version"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  uninstall - Remove from /usr/local/bin"
	@echo "  setup     - Check development environment"
	@echo "  run       - Build and run the application"
	@echo "  dist      - Create distribution tarball"
	@echo "  help      - Show this help message"

# Dependency tracking
-include $(OBJECTS:.o=.d)

%.d: %.c
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@
