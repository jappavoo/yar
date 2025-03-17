#include <stdio.h>
#include <stdint.h>

#include "hexdump.h"
//012345678901234567890123456789012345678901234567890123456789012345678901234567
ssize_t
hexdump(FILE *file, uint8_t *mem, size_t bytes) 
{
  int lines = bytes/16;
  int j;
  for (j=0; j<lines; j++) {
    unsigned long long offset=j*16;
    fprintf(file, "%08llx:  ",(unsigned long long)mem + offset);
    for (int i=0;i<16;i++) {
      fprintf(file, "%02x  ", mem[offset+i]);
    }
    fprintf(file, "|");
    for (int i=0;i<16;i++) {
      unsigned char c=mem[offset+i];
      if (c>=' ' && c<='~')  { fprintf(stderr, "%c", c); }
      else  fprintf(stderr, ".");
    }
    fprintf(stderr, "|\n");
  }
  // deal with data that is left over that is less than 16 bytes

  unsigned long long offset=j*16;
  int remaining = bytes - offset;

  if (remaining) {
    fprintf(file, "%08llx:  ",(unsigned long long)mem + offset);
    for (int i=0;i<remaining;i++) {
      fprintf(file, "%02x  ", mem[offset+i]);
    }
    for (int i=0; i<16-remaining;i++) {
      fprintf(file, "    ");
    }
    fprintf(file, "|");
    for (int i=0;i<remaining;i++) {
      unsigned char c=mem[offset+i];
      if (c>=' ' && c<='~') {  fprintf(file, "%c", c); }
      else  fprintf(file, ".");
    }
    fprintf(file, "|\n");
  }
  return bytes;
}
