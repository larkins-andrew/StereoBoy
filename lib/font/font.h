/*
	"font.h", Written by Daniel C. MIT License.

	This file is C89.

	Compiler/preprocessor configs (optional):
	-D NO_LOWERCASE
	-D NO_SYMBOL
	-D NO_NUMBERS
	In case you are limited in space :)
*/
#ifndef FONT_H
#define FONT_H

#include "stdbool.h"

extern int font_width;
extern int font_height;

struct Font {
    char letter;
    bool code[7*5];
	char width;
	char height;
};

const struct Font * find_font_char(char c);
extern struct Font font[];

#endif