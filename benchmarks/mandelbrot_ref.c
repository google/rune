// C reference implementation of mandelbrot benchmark.
// Matches output of benchmarks/mandelbrot.rn byte-for-byte.
//
// Outputs a binary PBM (P4) image to stdout.
// Header: "P4\n<width> <height>\n", matching the official CLBG PBM output.
// Pixel encoding: 8 pixels per byte, MSB = leftmost pixel.
//   bit 0 = in Mandelbrot set (black), bit 1 = escaped (white).
// The Rune code tracks escaped pixels in `bits` (1=escaped), writes ~bits.

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    unsigned N = 200;
    if (argc > 1) N = (unsigned)atoi(argv[1]);

    unsigned width = N, height = N;
    unsigned maxX = (width + 7) / 8;
    unsigned maxIterations = 50;
    double limitSq = 4.0;

    // Rune: print "P4\n%u %u\n" % (width, height)
    printf("P4\n%u %u\n", width, height);

    double cr0[8], cr[8], ci[8];

    for (unsigned y = 0; y < height; y++) {
        double ci0 = 2.0 * (double)y / (double)height - 1.0;
        for (unsigned x = 0; x < maxX; x++) {
            for (unsigned k = 0; k < 8; k++) {
                cr0[k] = 2.0 * (double)(8 * x + k) / (double)width - 1.5;
            }
            for (unsigned i = 0; i < 8; i++) {
                cr[i] = cr0[i];
                ci[i] = ci0;
            }
            unsigned char bits = 0;
            for (unsigned i = 0; i < maxIterations && bits != 0xff; i++) {
                for (unsigned k = 0; k < 8; k++) {
                    // mask = 1u8 << (7u32 - k)
                    unsigned char mask = (unsigned char)(1 << (7 - k));
                    if ((bits & mask) == 0) {
                        double crk  = cr[k];
                        double cik  = ci[k];
                        double cr2k = crk * crk;
                        double ci2k = cik * cik;
                        cr[k] = cr2k - ci2k + cr0[k];
                        ci[k] = 2.0 * crk * cik + ci0;
                        if (cr2k + ci2k > limitSq) {
                            bits |= mask;
                        }
                    }
                }
            }
            fputc((unsigned char)(~bits), stdout);
        }
    }
    return 0;
}
