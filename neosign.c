/*
 * neosign.c
 *
 * Copyright (c) 2014 Jeremy Garff <jer @ jers.net>
 * Copyright (c) 2025 Brad Goodman < brad @ bradgoodman.com >
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 *     1.  Redistributions of source code must retain the above copyright notice, this list of
 *         conditions and the following disclaimer.
 *     2.  Redistributions in binary form must reproduce the above copyright notice, this list
 *         of conditions and the following disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *     3.  Neither the name of the owner nor the names of its contributors may be used to endorse
 *         or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

static char VERSION[] = "XX.YY.ZZ";

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>
#include "font8x8/font8x8_basic.h"

#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"
#include "version.h"

#include "ws2811.h"

#define ARRAY_SIZE(stuff)       (sizeof(stuff) / sizeof(stuff[0]))
#define SPACE_WIDTH 4

// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10
//#define STRIP_TYPE            WS2811_STRIP_RGB                // WS2812/SK6812RGB integrated chip+leds
#define STRIP_TYPE              WS2811_STRIP_GBR	// WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            SK6812_STRIP_RGBW               // SK6812RGBW (NOT SK6812RGB)

#define WIDTH                   8
#define HEIGHT                  8
#define LED_COUNT               (WIDTH * HEIGHT)

int brightness = 20;
int width = WIDTH;
int height = HEIGHT;
int led_count = LED_COUNT;
int pipe_fd = -1;
unsigned int fix_color = 0;
unsigned int *colorbuf=0;

int renderpos = 0;
// BKG Overall image to render and scroll
int charpos = 0;
int rendersize = 32;
int hue_phase = 0;
const int max_hue = 768;
int output_to_screen = 0;
unsigned char flash_message = 0;

int clear_on_exit = 0;
char *message = 0L;
char *pipename = 0L;

int xy2px(int x, int y);
uint32_t hb_to_rgb(int hue, int brightness)
{
	uint8_t r, g, b;

	int normalized_hue = hue % max_hue;
	if (normalized_hue < 0) {
		normalized_hue += max_hue;
	}

	if (brightness < 0)
		brightness = 0;
	if (brightness > 255)
		brightness = 255;

	if (normalized_hue < 256) {
		r = (uint8_t) ((255 * (255 - normalized_hue)) / 255);
		g = (uint8_t) ((normalized_hue * 255) / 255);
		b = 0;
	} else if (normalized_hue < 512) {
		r = 0;
		g = (uint8_t) ((255 * (511 - normalized_hue)) / 255);
		b = (uint8_t) (((normalized_hue - 256) * 255) / 255);
	} else {
		r = (uint8_t) (((normalized_hue - 512) * 255) / 255);
		g = 0;
		b = (uint8_t) ((255 * (767 - normalized_hue)) / 255);
	}

	// Apply brightness
	r = (uint8_t) ((r * brightness) / 255);
	g = (uint8_t) ((g * brightness) / 255);
	b = (uint8_t) ((b * brightness) / 255);

	// Combine into 00BBGGRR format
	uint32_t result = (b << 16) | (g << 8) | r;
	return result;
}

ws2811_t ledstring = {
	.freq = TARGET_FREQ,
	.dmanum = DMA,
	.channel = {
		    [0] = {
			   .gpionum = GPIO_PIN,
			   .invert = 0,
			   .count = LED_COUNT,
			   .strip_type = STRIP_TYPE,
			   .brightness = 255,
			   },
		    [1] = {
			   .gpionum = 0,
			   .invert = 0,
			   .count = 0,
			   .brightness = 0,
			   },
		    },
};

ws2811_led_t *matrix;

static uint8_t running = 1;

void print_color_array(uint32_t *color,int size,int offset);
void matrix_to_screen(void);
// Send matrix buffer to actual led buffer
void matrix_render(void)
{
	int x, y;

	if (output_to_screen) {
		matrix_to_screen();
		return;
	}
	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++) {
			ledstring.channel[0].leds[(y * width) + x] =
			    matrix[y * width + x];

		}
	}
}

void matrix_to_screen(void)
{
	int x, y;
	printf("\033[2J");
	printf("\033[999A");
    uint32_t lastc=0;
    uint32_t last;
	for (x = 0; x < height; x++) {
		if (!(x % 10))
			printf("%d", x / 10);
		else
			printf(" ");
	}
	printf("\n");
	for (x = 0; x < height; x++) {
		if (x % 10)
			printf("%d", x % 10);
		else
			printf("-");
	}
	printf("\n");

	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++) {
            last = matrix[xy2px(x, y)];
            if (last && (last != lastc)){
                    printf ("\e[38;2;%d;%d;%dm",(last >> 16) & 0xff,(last >> 8) & 0xff,(last) & 0xff);
                    //printf ("\e[38;2;%d;%d;%dm",(last >> 16) & 0xff,255,(last) & 0xff);
                    //printf ("\e[38;2;%d;%d;%dm",255,255,255);
                    lastc = last;
            }
			if (last) {
				printf("\u2588");
			} else {
				printf(" ");
			}
		}
		printf("\n");
	}
    if (colorbuf) {
    for (y = 0; y < height; y++) {
            if (colorbuf[y]) 
                    printf("^");
            else
                    printf(" ");
    }}
    printf("\n");
}

// Shift everything over one dot in x direction?
void matrix_raise(void)
{
	int x, y;

	for (y = 0; y < (height - 1); y++) {
		for (x = 0; x < width; x++) {
			// This is for the 8x8 Pimoroni Unicorn-HAT where the LEDS in subsequent
			// rows are arranged in opposite directions
			matrix[y * width + x] =
			    matrix[(y + 1) * width + width - x - 1];
		}
	}
}

// The way display is daisy-chained, each column is flipped
int xy2px(int x, int y)
{
	int xx = x;
	if (y % 2) {
		xx = 7 - x;
	}
	return (((y) * width) + xx);
}

// Scroll one pixel from Right to Left
void matrix_bkg_shift(void)
{
	int x, y;

	for (y = 1; y <= (height - 1); y++) {
		for (x = 0; x < width; x++) {
			//matrix[((y-1) * width) + x]  = matrix[((y) * width) + x];
			matrix[xy2px(x, y - 1)] = matrix[xy2px(x, y)];
		}
	}
}

void matrix_clear(void)
{
	int x, y;

	for (y = 0; y < (height); y++) {
		for (x = 0; x < width; x++) {
			matrix[y * width + x] = 0;
		}
	}
}

int dotspos[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

ws2811_led_t dotcolors[] = {
	0x00200000,		// red
	0x00201000,		// orange
	0x00202000,		// yellow
	0x00002000,		// green
	0x00002020,		// lightblue
	0x00000020,		// blue
	0x00100010,		// purple
	0x00200010,		// pink
};

ws2811_led_t dotcolors_rgbw[] = {
	0x00200000,		// red
	0x10200000,		// red + W
	0x00002000,		// green
	0x10002000,		// green + W
	0x00000020,		// blue
	0x10000020,		// blue + W
	0x00101010,		// white
	0x10101010,		// white + W

};

// Add stuff to last row??
//font8x8_basic[128][8]
//
unsigned char font_firstpos[128];
unsigned char font_lastpos[128];
// Detect character spacing - build table
void init_font()
{
	int ch;
	for (ch = 32; ch < 128; ch++) {
		int x;
		unsigned int det = 0;
		for (x = 0; x < 8; x++) {
			int y;
			int yy = 0;
			for (y = 0; y < 8; y++) {
				if (font8x8_basic[ch][y] & (1 << x)) {
					//                   printf("X");
					yy |= (1 << y);
				} else {
					//         printf(" ");
				}
			}

			if (yy == 0) {
				if (!det) {
					font_firstpos[ch] = x;
				}
				//       printf("  <--- EMPTY (%d)",x);
			} else {
				det = 1;
				font_lastpos[ch] = x;
			}
			// printf("\n");
		}
		//printf ("Char %c %d First=%d Last=%d\n",ch,ch,font_firstpos[ch],font_lastpos[ch]);
	}

	// Space character requires actual space
	font_firstpos[32] = 0;
	font_lastpos[32] = 4;
}

int get_word_space(char *word)
{
	int space = 0;
	while (*word) {
		// Less than 32 is a control sequence

        if (*word == 27) {
                if (word[1]=='c')
                        word += 8; //skip color data - TODO buffer check!
        }
        else if (*word == 32) {
			space += 4;
		} else if (*word > 32) {
			space += font_lastpos[*word] - font_firstpos[*word];
			space += 2;	// Space between characters
			word++;
		}
	}
	return (space - 1);	// Drop space after last character
}

// Build list of things to display by
struct screen_s;
typedef struct screen_s *screen_p;
struct screen_s {
	char *text;
    uint32_t *color;
	int size;
	screen_p next;
};

void free_word_array(screen_p a)
{
	screen_p next;
	while (a) {
		free(a->text);
		free(a->color);
		next = a->next;
		free(a);
	}
}

// Function to build the linked list of words that fit the screen width
screen_p build_word_array(const char *input_string, const uint32_t *color)
{
	if (input_string == NULL || *input_string == '\0') {
		return NULL;
	}

    printf("INPUT STRING \"%s\"\nINPUT COLORS: ",input_string);
    print_color_array(color,strlen(input_string),0);

	screen_p head = NULL;
	screen_p current_node = NULL;
	char *current_line = NULL;
	int current_line_width = 0;
    uint32_t charpos;
	uint32_t *current_color = NULL;

	// Create a mutable copy of the input string for strtok
	char *str_copy = strdup(input_string);
	if (str_copy == NULL) {
		perror("strdup failed");
		return NULL;
	}

	char *word = strtok(str_copy, " ");
	while (word != NULL) {
        charpos = word-str_copy;
		int word_width = get_word_space(word);
		int potential_width;

		if (current_line == NULL) {
			potential_width = word_width;
		} else {
			potential_width =
			    current_line_width + SPACE_WIDTH + word_width;
		}

		if (potential_width <=height) {
			// Word fits on the current line
			if (current_line == NULL) {
				current_line = strdup(word);
				if (current_line == NULL) {
					perror("strdup failed");
					// Need to free previously allocated memory if this happens in a real scenario
					free(str_copy);
					free(current_color);
					return head;
				}
				current_color = malloc(sizeof(uint32_t)*strlen(word));
                memcpy(current_color,&color[charpos],strlen(word)*sizeof(uint32_t));
                printf("First Line \"%s\": (p=%d len=%d)",word,charpos,strlen(word));
                print_color_array(current_color,strlen(word),charpos);
				current_line_width = word_width;
			} else {
				// Append space and then the word
				size_t new_len = strlen(current_line) + 1 + strlen(word) + 1;	// +1 for space, +1 for null terminator
                //printf("line realoc %p %s\n",current_line,current_line);
				char *temp = realloc(current_line, new_len);
                //printf("line realoc done\n");
				if (temp == NULL) {
					perror("realloc failed");
					// Need to free previously allocated memory if this happens in a real scenario
					free(str_copy);
					free(current_line);
					return head;
				}
				current_line = temp;
                printf("AMMEND \"%s\" + \"%s\"\n",current_line,word);
                int orig_strlen = strlen(current_line);
				strcat(current_line, " ");
				strcat(current_line, word);
                int new_strlen = strlen(current_line);
                int add_width = SPACE_WIDTH + word_width;
                //printf("REALLOC clw=%d ww=%d \"%s\" SIZE %ld\n",current_line_width,word_width,current_line,sizeof(uint32_t)*(current_line_width+add_width));
                //current_color = realloc(current_color,sizeof(uint32_t)*(current_line_width+add_width));
                //printf("Realloc deon\n");
                //memset(&current_color[current_line_width],0,add_width);
				current_color = realloc(current_color,sizeof(uint32_t)*new_strlen);
                //current_color[charpos+orig_strlen+1]=0; //space
                memcpy(current_color,&color[charpos],new_strlen*sizeof(uint32_t));
                printf("Ammend Line \"%s\": ",current_line);
                print_color_array(current_color,strlen(current_line),charpos);
				current_line_width += add_width;

			}
		} else {
			// Word doesn't fit, start a new line
			if (current_line != NULL) {
				// Create a new node for the current line
				screen_p new_node =
				    (screen_p) malloc(sizeof(struct screen_s));
				if (new_node == NULL) {
					perror("malloc failed");
					// Need to free previously allocated memory if this happens in a real scenario
					free(str_copy);
					free(current_line);
					// Free the linked list created so far
					free_word_array(head);
					return NULL;
				}
				new_node->text = current_line;
				new_node->size = current_line_width;
                new_node->color= current_color;
				new_node->next = NULL;

				if (head == NULL) {
					head = new_node;
					current_node = new_node;
				} else {
					current_node->next = new_node;
					current_node = new_node;
				}
				current_line = NULL;
				current_line_width = 0;
			}

			// Start a new line with the current word
			current_line = strdup(word);
			if (current_line == NULL) {
				perror("strdup failed");
				free(str_copy);
				free_word_array(head);
				return NULL;
			}
			current_color = malloc(sizeof(uint32_t)*strlen(word));
            memcpy(current_color,&color[charpos],strlen(word)*sizeof(uint32_t));
            printf("New Line: (cp=%d) \"%s\" ",charpos,current_line);
            print_color_array(current_color,strlen(word),charpos);
			current_line_width = word_width;
		}

		word = strtok(NULL, " ");
	}

	// Add the last line if it exists
	if (current_line != NULL) {
		screen_p new_node = (screen_p) malloc(sizeof(struct screen_s));
		if (new_node == NULL) {
			perror("malloc failed");
			free(str_copy);
			free(current_line);
			free(current_color);
			free_word_array(head);
			return NULL;
		}
		new_node->text = current_line;
		new_node->size = current_line_width;
		new_node->color = current_color;
		new_node->next = NULL;

		if (head == NULL) {
			head = new_node;
		} else {
			current_node->next = new_node;
		}
	}

	free(str_copy);
	return head;
}

void print_encoded_string(char *c) {
        for (;*c;c++) {
                if (*c>=32) {
                        printf("%c",*c);
                } else {
                        printf("[%2.2x]",*c);
                }
        }
}
void print_color_array(uint32_t *color,int size,int offset) {
        int i;
        printf("COLOR ARRAY (o=%d s=%d): ",offset,size);
            for (i=0;i<size;i++) {
                    if (color[i]) {
                            printf("%d:[%6.6x] ",i+offset,color[i]);
                    }
            }
        printf("\n");
}
void print_word_array(screen_p a)
{
    int i;
	do {
		printf("String \"");
        print_encoded_string(a->text);
        printf("\" len %d -- ",  a->size);
        printf("\n");
        print_color_array(a->color,a->size,0);
	}
	while (a = a->next);
}

// Split message string into color and message buffers
void decodeMessageString(char *s) {
        char *c;
        char *m = malloc(strlen(s));
        int l=0;
        colorbuf = malloc(strlen(s)*sizeof(uint32_t));
        colorbuf[0] = 0;
        for (c=s;*c;c++) {
                if ((*c == 27) && (c[1]=='c')){ 
                    char end;
                    end = c[8];
                    c[8] = (char) 0;
                    colorbuf[l] = strtoul(&c[2],0L,16);
                    c[8]=end;
                    c += 7;
                }  else {
                        m[l]=*c;
                        l++;
                        colorbuf[l] = 0;
                }
        }
        m[l]=(char) 0;
        message=m;
		rendersize = (strlen(message) * 8) + 16;
}

// Write a SPECIFIC word-array entry to display buffer
int display_array_entry(screen_p a, int startpos)
{
	int i = 0;
	int x;
	int charpos;
	// Clear entire display
	matrix_clear();

	// FIX - If larger than length - deal with scrolling
	// Start position
	if (startpos == 0) {
		x = (height - a->size) / 2;
		if (x < 0)
			x = 0;
	} else {
		x = -startpos;
	}

	char *ch;
	unsigned int c;
	for (ch = a->text; *ch; ch++) {
            /*
        if (*ch == 27) {
                if (ch[1] == 'c') {
                        char *endptr = &ch[7];
                        //print_encoded_string(&ch[1]);
                        fix_color = strtoul(&ch[2],&endptr,16);
                        ch += 7;
                        //printf("NewFixColor %x = Next: ",fix_color);
                        //print_encoded_string(&ch[1]);
                        //exit(0);

                }
                continue;
        } else 
        */
        if (*ch == 32) {

        
			x += SPACE_WIDTH;
			continue;
		}
		for (charpos = font_firstpos[*ch]; charpos <= font_lastpos[*ch];
		     charpos++) {
			for (i = 0; i < width; i++) {
				if ((font8x8_basic[*ch][i] >> (charpos)) & 1) {
					if (fix_color == 0)
						c = hb_to_rgb(hue_phase,
							      brightness);
					else
						c = fix_color;
				} else {
					c = 0;
				}

				if ((x < height) && (x >= 0)) {
					matrix[xy2px(i, x)] = c;
				}
			}
			x++;
			if (x >= height) {
				return (startpos + 1);
			}
		}
		x++;
		if (x >= height) {
			return (startpos + 1);
		}
	}
	return (0);
}

void display_word_array(screen_p a)
{
	do {
			int offset = 0;
		if (a->size > height) {
			// If it's too big - we need to scroll!
			printf("SCROLL String \"%s\" len %d\n", a->text,
			       a->size);
		} else {
			// If it fits - we need to center it
			offset = (height - a->size) / 2;
			printf("CENTER String \"%s\" len %d offset=%d\n",
			       a->text, a->size, offset);
		}
        if (a->color)
        print_color_array(a->color,strlen(a->text),offset);
        printf("\n");
	}
	while (a = a->next);
}

// This draws only the LAST roll of scrolling text
// (Because it's called after we've scrolled/shifted)
// This assumes characters are 8 bits wide, with code to trim trailing space
// Colorbuf has one entry per character
void matrix_bottom(void)
{
	int i;
	int c;
	char ch = ' ';
	unsigned char allzero = 1;
	if (message) {
		if ((charpos / 8) <= strlen(message)) {
			ch = message[charpos / 8];
		}
	}

    if (colorbuf && colorbuf[charpos>>3] && colorbuf[charpos>>3] != fix_color) {
            fix_color=colorbuf[charpos>>3];
    }

	for (i = 0; i < width; i++) {
		if ((font8x8_basic[ch][7 - i] >> (charpos % width)) & 1) {
			//c = 0x00002000;
			if (fix_color == 0)
				c = hb_to_rgb(hue_phase, brightness);
			else
				c = fix_color;
		} else {
			c = 0;
		}

		// This kills trailing space in characters
		if ((font8x8_basic[ch][7 - i] >> ((charpos % width))))
			allzero = 0;

		matrix[((height - 1) * width) + i] = c;
	}

	// Make space character 3 spaces max
	if ((ch == ' ') && ((charpos % width) < 2))
		allzero = 0;
	// if (ch == ' ') allzero=0;

	renderpos = (renderpos + 1) % rendersize;
	if (allzero) {
		//Skip to next character - eliminate excess whitepace
		charpos = ((charpos & ~0x07) + 8) % rendersize;
	} else {
		charpos = (charpos + 1) % rendersize;
	}
	hue_phase = (hue_phase + 1) % max_hue;
}

// Brad's drawing function (UNUSED???)
void matrix_bkg(void)
{
	int i;

	for (i = 0; i < (int)(ARRAY_SIZE(dotspos)); i++) {
		if (ledstring.channel[0].strip_type == SK6812_STRIP_RGBW) {
			matrix[dotspos[i] + (height - 1) * width] =
			    dotcolors_rgbw[i];
		} else {
			matrix[dotspos[i] + (height - 1) * width] =
			    dotcolors[i];
		}
	}

}

static void ctrl_c_handler(int signum)
{
	(void)(signum);
	running = 0;
}

static void setup_handlers(void)
{
	struct sigaction sa = {
		.sa_handler = ctrl_c_handler,
	};

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

void parseargs(int argc, char **argv, ws2811_t * ws2811)
{
	int index;
	int c;

	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"screen", no_argument, 0, 'S'},
		{"dma", required_argument, 0, 'd'},
		{"brightness", required_argument, 0, 'b'},
		{"color", required_argument, 0, 'C'},
		{"gpio", required_argument, 0, 'g'},
		{"invert", no_argument, 0, 'i'},
		{"clear", no_argument, 0, 'c'},
		{"strip", required_argument, 0, 's'},
		{"message", required_argument, 0, 'm'},
		{"pipe", required_argument, 0, 'p'},
		{"height", required_argument, 0, 'y'},
		{"width", required_argument, 0, 'x'},
		{"version", no_argument, 0, 'v'},
		{"flash", no_argument, 0, 'f'},
		{0, 0, 0, 0}
	};

	while (1) {

		index = 0;
		c = getopt_long(argc, argv, "fSC:b:p:cd:g:his:vx:y:m:",
				longopts, &index);

		if (c == -1)
			break;

		switch (c) {
		case 0:
			/* handle flag options (array's 3rd field non-0) */
			break;

		case 'h':
			fprintf(stderr, "%s version %s\n", argv[0], VERSION);
			fprintf(stderr, "Usage: %s \n"
				"-b {0-255}     - Brightness\n"
				"-h (--help)    - this information\n"
				"-s (--strip)   - strip type - rgb, grb, gbr, rgbw\n"
				"-m (--message) - Message text\n"
				"-f (--flash)   - Flash message (instead of scroll\n"
				"-x (--width)   - matrix width (default 8)\n"
				"-y (--height)  - matrix height (default 8)\n"
				"-d (--dma)     - dma channel to use (default 10)\n"
				"-g (--gpio)    - GPIO to use\n"
				"                 If omitted, default is 18 (PWM0)\n"
				"-i (--invert)  - invert pin output (pulse LOW)\n"
				"-c (--clear)   - clear matrix on exit.\n"
				"-C (--color)   - Used fix RGB hexvalue like bbggrr\n"
				"-S             - Output to screen\n"
				"-p (--pipe)    - Shared pipe filename for text updates\n"
				"-v (--version) - version information\n",
				argv[0]);
			exit(-1);

		case 'f':
			flash_message = 1;
			break;
		case 'D':
			break;

		case 'g':
			if (optarg) {
				int gpio = atoi(optarg);
/*
	PWM0, which can be set to use GPIOs 12, 18, 40, and 52.
	Only 12 (pin 32) and 18 (pin 12) are available on the B+/2B/3B
	PWM1 which can be set to use GPIOs 13, 19, 41, 45 and 53.
	Only 13 is available on the B+/2B/PiZero/3B, on pin 33
	PCM_DOUT, which can be set to use GPIOs 21 and 31.
	Only 21 is available on the B+/2B/PiZero/3B, on pin 40.
	SPI0-MOSI is available on GPIOs 10 and 38.
	Only GPIO 10 is available on all models.

	The library checks if the specified gpio is available
	on the specific model (from model B rev 1 till 3B)

*/
				ws2811->channel[0].gpionum = gpio;
			}
			break;

		case 'i':
			ws2811->channel[0].invert = 1;
			break;

		case 'b':
			if (optarg) {
				brightness = atoi(optarg);
			}
			break;
		case 'c':
			clear_on_exit = 1;
			break;
		case 'S':
			output_to_screen = 1;
			break;
		case 'C':
			if (optarg) {
				fix_color = strtoul(optarg, 0L, 16);
			}
			break;

		case 'd':
			if (optarg) {
				int dma = atoi(optarg);
				if (dma < 14) {
					ws2811->dmanum = dma;
				} else {
					printf("invalid dma %d\n", dma);
					exit(-1);
				}
			}
			break;

		case 'y':
			if (optarg) {
				height = atoi(optarg);
				if (height > 0) {
					ws2811->channel[0].count =
					    height * width;
				} else {
					printf("invalid height %d\n", height);
					exit(-1);
				}
			}
			break;

		case 'x':
			if (optarg) {
				width = atoi(optarg);
				if (width > 0) {
					ws2811->channel[0].count =
					    height * width;
				} else {
					printf("invalid width %d\n", width);
					exit(-1);
				}
			}
			break;

		case 'm':
			if (optarg) {
                decodeMessageString(optarg);
                rendersize = (strlen(message) * 8) + 16;
			}
			break;
		case 'p':
			if (optarg) {
				pipename = strdup(optarg);
			}
			break;
		case 's':
			if (optarg) {
				if (!strncasecmp("rgb", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    WS2811_STRIP_RGB;
				} else if (!strncasecmp("rbg", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    WS2811_STRIP_RBG;
				} else if (!strncasecmp("grb", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    WS2811_STRIP_GRB;
				} else if (!strncasecmp("gbr", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    WS2811_STRIP_GBR;
				} else if (!strncasecmp("brg", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    WS2811_STRIP_BRG;
				} else if (!strncasecmp("bgr", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    WS2811_STRIP_BGR;
				} else if (!strncasecmp("rgbw", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    SK6812_STRIP_RGBW;
				} else if (!strncasecmp("grbw", optarg, 4)) {
					ws2811->channel[0].strip_type =
					    SK6812_STRIP_GRBW;
				} else {
					printf("invalid strip %s\n", optarg);
					exit(-1);
				}
			}
			break;

		case 'v':
			fprintf(stderr, "%s version %s\n", argv[0], VERSION);
			exit(-1);

		case '?':
			/* getopt_long already reported error? */
			exit(-1);

		default:
			exit(-1);
		}
	}
}

int main(int argc, char *argv[])
{
	ws2811_return_t ret;
	int bytes_read;
	char new_message[1024];

	sprintf(VERSION, "%d.%d.%d", VERSION_MAJOR, VERSION_MINOR,
		VERSION_MICRO);

	parseargs(argc, argv, &ledstring);

	init_font();

	matrix = malloc(sizeof(ws2811_led_t) * width * height);

	setup_handlers();

	screen_p flash_orig = build_word_array(message,colorbuf);
	screen_p flash = flash_orig;
	display_word_array(flash);
    //exit(1); // REMOVE

	if (!output_to_screen) {
		if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
			fprintf(stderr, "ws2811_init failed: %s\n",
				ws2811_get_return_t_str(ret));
			return ret;
		}
	}

	if (pipename) {
		if (mkfifo(pipename, 0666) == -1 && errno != EEXIST) {
			perror("Failed to create pipe");
			return 1;
		}
		pipe_fd = open(pipename, O_RDONLY | O_NONBLOCK);
		if (pipe_fd == -1) {
			perror("failed to open shared pipe");
			return 1;
		}
	}

	int fmr = 0;
	while (running) {
		//matrix_raise();
		if (flash_message) {
			fmr = display_array_entry(flash, fmr);
            matrix_render();	// Send to hw buffer
            //printf("FMR=%d\n",fmr);
			if (fmr==1) {
				sleep(1);
            }
            else if (fmr) {
				//usleep(1000000 / 1500);
			} else {
				sleep(1);
				if (!flash->next) {
					flash = flash_orig;
				    sleep(1);
				} else {
					flash = flash->next;
				}
			}
		} else {
			matrix_bkg_shift();	// Scroll one pixel
			matrix_bottom();
            matrix_render();	// Send to hw buffer
		}

		if (!output_to_screen) {
			if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
				fprintf(stderr, "ws2811_render failed: %s\n",
					ws2811_get_return_t_str(ret));
				break;
			}
		}

		// 15 frames /sec
		usleep(1000000 / 15);
		//sleep(1);
		if (pipe_fd != -1) {
			bytes_read =
			    read(pipe_fd, new_message, sizeof(new_message));
			if (bytes_read >= 1) {
				if (message)
					free(message);
                if (colorbuf)
                    free(colorbuf);
                new_message[bytes_read]=(char) 0;
				message = strndup(new_message, bytes_read);
				matrix_clear();
				renderpos = 0;
				charpos = 0;
			}
		}
	}

	if (clear_on_exit) {
		matrix_clear();
		matrix_render();
		ws2811_render(&ledstring);
	}

	if (!output_to_screen) {
		ws2811_fini(&ledstring);
	} else {
            printf("\033[0m");
    }

	printf("\n");
	return ret;
}
