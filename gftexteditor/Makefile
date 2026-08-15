# ============================================================================
# Makefile - GF Text Editor
# Autore: Graziano Falcone <exagonx@hotmail.com> - Licenza GNU GPL v2
#
# Struttura:
#   src/   sorgenti (.c e .h)
#   obj/   file oggetto generati (obj/linux o obj/win, creata automaticamente)
#   bin/   eseguibili finali generati (creata automaticamente)
#
# Uso:
#   make            -> build nativa Linux (richiede libncursesw-dev e pkg-config)
#   make windows    -> cross-compilazione per Windows con MinGW-w64
#   make clean      -> pulisce obj/ e bin/
#
# Su Debian/Ubuntu, se manca pkg-config o ncursesw/ncurses.h:
#   sudo apt update
#   sudo apt install pkg-config libncurses-dev
#
# --- Windows: prerequisiti per "make windows" -------------------------------
# 1) Cross-compiler: sudo apt install mingw-w64 (o mingw-w64-x86_64-toolchain
#    su MSYS2). Va usata la variante "posix" (supporto pthread):
#       sudo update-alternatives --set x86_64-w64-mingw32-gcc \
#            /usr/bin/x86_64-w64-mingw32-gcc-posix
# 2) PDCursesMod (non inclusa: ncurses non esiste su Windows):
#       git clone --depth 1 https://github.com/Bill-Gray/PDCursesMod.git
#       cd PDCursesMod/wincon
#       make CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar \
#            WIDE=Y UTF8=Y pdcurses.a
#    poi: make windows PDCURSES_DIR=/percorso/PDCursesMod
# ============================================================================

SRC_DIR   = src
BIN_DIR   = bin
TARGET    = gf_edit
SOURCES   = $(wildcard $(SRC_DIR)/*.c)

# --- Build Linux (nativa) ---------------------------------------------------
CC        = gcc
CFLAGS    = -Wall -Wextra -std=c99 -pthread $(shell pkg-config --cflags ncursesw)
LDFLAGS   = $(shell pkg-config --libs ncursesw) -lpthread
OBJ_DIR   = obj/linux
OBJS      = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/gf_texteditor.h $(SRC_DIR)/gf_internal.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# --- Build Windows (cross-compilazione con MinGW-w64) -----------------------
WIN_CC      = x86_64-w64-mingw32-gcc
PDCURSES_DIR ?= /opt/PDCursesMod
WIN_OBJ_DIR = obj/win
WIN_CFLAGS  = -Wall -Wextra -std=c99 -pthread -I$(PDCURSES_DIR)
WIN_LDFLAGS = $(PDCURSES_DIR)/wincon/pdcurses.a -lpthread -lwinmm -mconsole
WIN_OBJS    = $(patsubst $(SRC_DIR)/%.c,$(WIN_OBJ_DIR)/%.o,$(SOURCES))

windows: $(BIN_DIR)/$(TARGET).exe

$(BIN_DIR)/$(TARGET).exe: $(WIN_OBJS) | $(BIN_DIR)
	$(WIN_CC) $(WIN_OBJS) -o $@ $(WIN_LDFLAGS)

$(WIN_OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/gf_texteditor.h $(SRC_DIR)/gf_internal.h | $(WIN_OBJ_DIR)
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

$(WIN_OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf obj bin

.PHONY: all windows clean
