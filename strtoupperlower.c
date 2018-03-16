#include <ctype.h>

void strtoupper(char *string) {
        while(*string != '\0') {
                *string = toupper(*string);
                string++;
        }
}

void strtolower(char *string) {
        while(*string != '\0') {
                *string = tolower(*string);
                string++;
        }
}

