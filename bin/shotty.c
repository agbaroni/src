/* Copyright (C) 2019  C. McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <err.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sysexits.h>
#include <unistd.h>
#include <wchar.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef unsigned uint;

enum {
	NUL, SOH, STX, ETX, EOT, ENQ, ACK, BEL,
	BS, HT, NL, VT, NP, CR, SO, SI,
	DLE, DC1, DC2, DC3, DC4, NAK, SYN, ETB,
	CAN, EM, SUB, ESC, FS, GS, RS, US,
	DEL = 0x7F,
};

struct Style {
	bool bold, italic, underline, reverse;
	uint bg, fg;
};

struct Cell {
	struct Style style;
	wchar_t ch;
};

static struct Style def = { .fg = 7 };
static uint rows, cols;

static uint y, x;
static struct Style style;
static struct Cell *cells;

static struct Cell *cell(uint y, uint x) {
	return &cells[y * cols + x];
}

static char updateNUL(wchar_t ch) {
	switch (ch) {
		case BS: if (x) x--; return NUL;
		case NL: y = MIN(y + 1, rows - 1); x = 0; return NUL;
		case CR: x = 0; return NUL;
		case ESC: return ESC;
	}
	if (ch < ' ') {
		warnx("unhandled \\x%02X", ch);
		return NUL;
	}

	int width = wcwidth(ch);
	if (width < 0) {
		warnx("unhandled \\u%X", ch);
		return NUL;
	}

	cell(y, x)->style = style;
	cell(y, x)->ch = ch;

	for (int i = 1; i < width; ++i) {
		cell(y, x + i)->style = style;
		cell(y, x + i)->ch = '\0';
	}
	x = MIN(x + width, cols - 1);

	return NUL;
}

enum {
	CSI = '[',
	CUU = 'A',
	CUD,
	CUF,
	CUB,
	CNL,
	CPL,
	CHA,
	CUP,
	ED = 'J',
	EL,
	VPA = 'd',
	SM = 'h',
	RM = 'l',
	SGR,
};

static char updateESC(wchar_t ch) {
	static bool discard;
	if (discard) {
		discard = false;
		return NUL;
	}
	switch (ch) {
		case '(': discard = true; return ESC;
		case '=': return NUL;
		case CSI: return CSI;
		default: warnx("unhandled ESC %lc", ch); return NUL;
	}
}

static char updateCSI(wchar_t ch) {
	static bool dec;
	if (ch == '?') {
		dec = true;
		return CSI;
	}

	static uint n, p, ps[8];
	if (ch == ';') {
		n++;
		p++;
		ps[p %= 8] = 0;
		return CSI;
	}
	if (ch >= '0' && ch <= '9') {
		ps[p] *= 10;
		ps[p] += ch - '0';
		if (!n) n++;
		return CSI;
	}

	switch (ch) {
		break; case CUU: y -= MIN((n ? ps[0] : 1), y);
		break; case CUD: y  = MIN(y + (n ? ps[0] : 1), rows - 1);
		break; case CUF: x  = MIN(x + (n ? ps[0] : 1), cols - 1);
		break; case CUB: x -= MIN((n ? ps[0] : 1), x);
		break; case CNL: y  = MIN(y + (n ? ps[0] : 1), rows - 1); x = 0;
		break; case CPL: y -= MIN((n ? ps[0] : 1), y); x = 0;
		break; case CHA: x  = MIN((n ? ps[0] - 1 : 0), cols - 1);
		break; case VPA: y  = MIN((n ? ps[0] - 1 : 0), rows - 1);
		break; case CUP: {
			y = MIN((n > 0 ? ps[0] - 1 : 0), rows - 1);
			x = MIN((n > 1 ? ps[1] - 1 : 0), cols - 1);
		}

		break; case ED: {
			struct Cell *from = cell(0, 0);
			struct Cell *to = cell(rows - 1, cols - 1);
			if (ps[0] == 0) from = cell(y, x);
			if (ps[0] == 1) to = cell(y, x);
			for (struct Cell *clear = from; clear <= to; ++clear) {
				clear->style = style;
				clear->ch = ' ';
			}
		}
		break; case EL: {
			struct Cell *from = cell(y, 0);
			struct Cell *to = cell(y, cols - 1);
			if (ps[0] == 0) from = cell(y, x);
			if (ps[0] == 1) to = cell(y, x);
			for (struct Cell *clear = from; clear <= to; ++clear) {
				clear->style = style;
				clear->ch = ' ';
			}
		}

		break; case SM: // ignore
		break; case RM: // ignore

		break; case SGR: {
			for (uint i = 0; i < p + 1; ++i) {
				switch (ps[i]) {
					break; case 0: style = def;
					break; case 1: style.bold = true;
					break; case 3: style.italic = true;
					break; case 4: style.underline = true;
					break; case 7: style.reverse = true;
					break; case 21: style.bold = false;
					break; case 22: style.bold = false;
					break; case 23: style.italic = false;
					break; case 24: style.underline = false;
					break; case 27: style.reverse = false;
					break; case 39: style.fg = def.fg;
					break; case 49: style.bg = def.bg;
					break; default: {
						if (ps[i] >= 30 && ps[i] <= 37) {
							style.fg = ps[i] - 30;
						} else if (ps[i] >= 40 && ps[i] <= 47) {
							style.bg = ps[i] - 40;
						} else if (ps[i] >= 90 && ps[i] <= 97) {
							style.fg = 7 + ps[i] - 90;
						} else if (ps[i] >= 100 && ps[i] <= 107) {
							style.bg = 7 + ps[i] - 100;
						} else {
							warnx("unhandled SGR %u", ps[i]);
						}
					}
				}
			}
		}

		break; default: warnx("unhandled CSI %lc", ch);
	}

	dec = false;
	ps[n = p = 0] = 0;
	return NUL;
}

static void update(wchar_t ch) {
	static char seq;
	switch (seq) {
		break; case NUL: seq = updateNUL(ch);
		break; case ESC: seq = updateESC(ch);
		break; case CSI: seq = updateCSI(ch);
	}
}

static void
html(const struct Style *prev, const struct Cell *cell) {
	if (!prev || memcmp(&cell->style, prev, sizeof(cell->style))) {
		if (prev) {
			if (prev->bold) printf("</b>");
			if (prev->italic) printf("</i>");
			if (prev->underline) printf("</u>");
			printf("</span>");
		}
		uint bg = (cell->style.reverse ? cell->style.fg : cell->style.bg);
		uint fg = (cell->style.reverse ? cell->style.bg : cell->style.fg);
		printf("<span class=\"bg%u fg%u\">", bg, fg);
		if (cell->style.bold) printf("<b>");
		if (cell->style.italic) printf("<i>");
		if (cell->style.underline) printf("<u>");
	}
	switch (cell->ch) {
		break; case '&': printf("&amp;");
		break; case '<': printf("&lt;");
		break; case '>': printf("&gt;");
		break; default:  printf("%lc", cell->ch);
	}
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");

	bool bright = false;
	FILE *file = stdin;

	int opt;
	while (0 < (opt = getopt(argc, argv, "Bb:f:h:w:"))) {
		switch (opt) {
			break; case 'B': bright = true;
			break; case 'b': def.bg = strtoul(optarg, NULL, 0);
			break; case 'f': def.fg = strtoul(optarg, NULL, 0);
			break; case 'h': rows = strtoul(optarg, NULL, 0);
			break; case 'w': cols = strtoul(optarg, NULL, 0);
			break; default:  return EX_USAGE;
		}
	}

	if (optind < argc) {
		file = fopen(argv[optind], "r");
		if (!file) err(EX_NOINPUT, "%s", argv[optind]);
	}

	if (!rows || !cols) {
		struct winsize window;
		int error = ioctl(STDERR_FILENO, TIOCGWINSZ, &window);
		if (error) err(EX_IOERR, "ioctl");
		if (!rows) rows = window.ws_row;
		if (!cols) cols = window.ws_col;
	}

	cells = calloc(rows * cols, sizeof(*cells));
	if (!cells) err(EX_OSERR, "calloc");

	style = def;
	for (uint i = 0; i < rows * cols; ++i) {
		cells[i].style = style;
		cells[i].ch = ' ';
	}

	wint_t ch;
	while (WEOF != (ch = getwc(file))) {
		update(ch);
	}
	if (ferror(file)) err(EX_IOERR, "getwc");

	if (bright) {
		for (uint i = 0; i < rows * cols; ++i) {
			if (!cells[i].style.bold || cells[i].style.fg > 7) continue;
			cells[i].style.bold = false;
			cells[i].style.fg += 8;
		}
	}

	printf(
		"<pre style=\"width: %uch;\" class=\"bg%u fg%u\">",
		cols, def.bg, def.fg
	);
	for (uint y = 0; y < rows; ++y) {
		for (uint x = 0; x < cols; ++x) {
			if (!cell(y, x)->ch) continue;
			html(x ? &cell(y, x - 1)->style : NULL, cell(y, x));
		}
		printf("</span>\n");
	}
	printf("</pre>\n");
}
