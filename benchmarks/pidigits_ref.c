/* pidigits_ref.c — reference implementation using GNU MP
 * Same Gibbons spigot algorithm as pidigits.rn; byte-identical output.
 * Compile: clang -O3 benchmarks/pidigits_ref.c -lgmp -o /tmp/claude-1000/pd_ref
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>

int main(int argc, char *argv[]) {
    int N = 27;
    if (argc > 1) N = atoi(argv[1]);

    mpz_t q, r, t, k, n, l;
    mpz_t tmp1, tmp2, tmp3;

    mpz_init_set_ui(q, 1);
    mpz_init_set_ui(r, 0);
    mpz_init_set_ui(t, 1);
    mpz_init_set_ui(k, 1);
    mpz_init_set_ui(n, 3);
    mpz_init_set_ui(l, 3);
    mpz_init(tmp1);
    mpz_init(tmp2);
    mpz_init(tmp3);

    int digitsProduced = 0;
    int col = 0;

    while (digitsProduced < N) {
        /* Check condition: 4*q + r - t < n*t  =>  4*q + r < (n+1)*t */
        mpz_mul_ui(tmp1, q, 4);
        mpz_add(tmp1, tmp1, r);
        mpz_mul(tmp2, n, t);
        mpz_add(tmp2, tmp2, t);  /* (n+1)*t */

        if (mpz_cmp(tmp1, tmp2) < 0) {
            /* Output digit */
            int digit = (int)mpz_get_ui(n);
            putchar('0' + digit);
            col++;
            digitsProduced++;

            if (col == 10) {
                printf("\t:%d\n", digitsProduced);
                col = 0;
            }

            /* Update: newQ = 10*q, newR = 10*(r - n*t), newN = 10*(3*q+r)/t - 10*n */
            mpz_mul_ui(tmp1, n, 1);  /* tmp1 = n */
            mpz_mul(tmp2, n, t);     /* tmp2 = n*t */
            mpz_sub(tmp3, r, tmp2);  /* tmp3 = r - n*t */
            mpz_mul_ui(tmp3, tmp3, 10); /* tmp3 = 10*(r-n*t) */

            /* newN = (10*(3*q+r))/t - 10*n */
            mpz_mul_ui(tmp2, q, 3);
            mpz_add(tmp2, tmp2, r);
            mpz_mul_ui(tmp2, tmp2, 10);
            mpz_tdiv_q(tmp2, tmp2, t);   /* tmp2 = 10*(3*q+r)/t */
            mpz_mul_ui(tmp1, n, 10);     /* tmp1 = 10*n */
            mpz_sub(tmp2, tmp2, tmp1);   /* tmp2 = newN */

            mpz_mul_ui(q, q, 10);        /* q = 10*q */
            mpz_set(r, tmp3);            /* r = 10*(r-n*t) */
            mpz_set(n, tmp2);            /* n = newN */
        } else {
            /* Step: q*=k, r=(2q+r)*l, t*=l, n=... */
            /* newN = (q*(7*k+2) + r*l) / (t*l) */
            mpz_mul_ui(tmp1, k, 7);
            mpz_add_ui(tmp1, tmp1, 2);
            mpz_mul(tmp1, q, tmp1);      /* tmp1 = q*(7*k+2) */
            mpz_mul(tmp2, r, l);         /* tmp2 = r*l */
            mpz_add(tmp1, tmp1, tmp2);   /* tmp1 = q*(7*k+2)+r*l */
            mpz_mul(tmp2, t, l);         /* tmp2 = t*l */
            mpz_tdiv_q(tmp1, tmp1, tmp2); /* tmp1 = newN */

            mpz_mul(tmp3, q, k);         /* tmp3 = q*k (newQ) */
            mpz_mul_ui(tmp2, q, 2);
            mpz_add(tmp2, tmp2, r);
            mpz_mul(tmp2, tmp2, l);      /* tmp2 = (2q+r)*l (newR) */
            mpz_mul(t, t, l);            /* t *= l */

            mpz_add_ui(l, l, 2);
            mpz_add_ui(k, k, 1);
            mpz_set(q, tmp3);
            mpz_set(r, tmp2);
            mpz_set(n, tmp1);
        }
    }

    /* Final partial row */
    if (col > 0) {
        int pad = col;
        while (pad < 10) {
            putchar(' ');
            pad++;
        }
        printf("\t:%d\n", N);
    }

    mpz_clear(q); mpz_clear(r); mpz_clear(t);
    mpz_clear(k); mpz_clear(n); mpz_clear(l);
    mpz_clear(tmp1); mpz_clear(tmp2); mpz_clear(tmp3);
    return 0;
}
