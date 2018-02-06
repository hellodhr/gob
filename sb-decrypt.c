/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <sodium.h>

#include "config.h"
#include "common.h"

#define PLAIN_LEN (BLOCK_LEN - crypto_aead_chacha20poly1305_ABYTES)

int main(int argc, char *argv[])
{
    unsigned char key[crypto_aead_chacha20poly1305_KEYBYTES];
    unsigned char nonce[crypto_aead_chacha20poly1305_NPUBBYTES];
    unsigned char *cipher = malloc(BLOCK_LEN);
    unsigned char *plain = malloc(PLAIN_LEN);
    ssize_t cipherlen;

    if (argc < 2)
        die("USAGE: %s <KEYFILE>", argv[0]);

    if (sodium_init() < 0)
        die("Unable to initialize libsodium");

    if (read_key(key, sizeof(key), argv[1]) < 0)
        die("Unable to read keyfile '%s'", argv[1]);

    memset(nonce, 0, sizeof(nonce));

    while ((cipherlen = read_bytes(STDIN_FILENO, cipher, BLOCK_LEN)) > 0) {
        size_t plainlen;

        if (crypto_aead_chacha20poly1305_decrypt(plain, (void *) &plainlen, NULL,
                cipher, cipherlen, NULL, 0, nonce, key) < 0)
            die("Unable to encrypt plaintext");

        memset(plain + plainlen, 0, PLAIN_LEN - plainlen);
        if (write_bytes(STDOUT_FILENO, plain, plainlen) < 0)
            die_errno("Unable to write ciphertext to stdout");

        sodium_increment(nonce, sizeof(nonce));
    }

    if (cipherlen < 0)
        die_errno("Unable to read from stdin");

    return 0;
}
