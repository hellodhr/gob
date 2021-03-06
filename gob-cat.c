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

#include <sys/stat.h>
#include <sys/types.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sodium.h>

#include "config.h"
#include "common.h"

static int parse_trailer(unsigned char *hash_out, size_t *datalen_out, const char *trailer)
{
    if (*trailer != '>')
        die("Last line is not a trailer line");
    trailer++;

    if (strlen(trailer) < HASH_LEN * 2)
        die("Trailer is too short");

    if (hex2bin(hash_out, HASH_LEN, trailer, HASH_LEN * 2) < 0)
        die("Unable to decode trailer hash");
    trailer += HASH_LEN * 2;

    if (*trailer != ' ')
        die("No separator between trailer hash and length");
    trailer++;

    if ((*datalen_out = strtol(trailer, NULL, 10)) == 0)
        die("Invalid data length in trailer");

    return 0;
}

int main(int argc, char *argv[])
{
    crypto_generichash_state *state = malloc(crypto_generichash_statebytes());
    unsigned char expected_trailer[HASH_LEN], computed_hash[HASH_LEN];
    unsigned char *block = malloc(BLOCK_LEN);
    char *line = NULL;
    ssize_t linelen;
    size_t total = 0, n = 0, expected_len;
    int storefd;

    if (argc != 2)
        die("USAGE: %s ( --version | <DIR> )", argv[0]);

    if (!strcmp(argv[1], "--version"))
        version("gob-cat");

    if ((storefd = open_store(argv[1])) < 0)
        die("Unable to open store");

    if (crypto_generichash_init(state, NULL, 0, HASH_LEN) < 0)
        die("Unable to initialize hashing state");

    while ((linelen = getline(&line, &n, stdin)) > 0) {
        ssize_t blocklen;
        int blockfd;

        if (*line == '>')
            break;

        if (line[linelen - 1] == '\n')
            line[--linelen] = '\0';

        if (strspn(line, "0123456789abcdef") != 2 * HASH_LEN)
            die("Invalid index hash '%s'", line);

        if ((blockfd = open_block(storefd, line, 0)) < 0)
            die_errno("Unable to open block '%s'", line);

        if ((blocklen = read_bytes(blockfd, block, BLOCK_LEN)) <= 0)
            die_errno("Unable to read block '%s'", line);

        if (crypto_generichash_update(state, block, blocklen) < 0)
            die("Unable to update hash");

        if (write_bytes(STDOUT_FILENO, block, blocklen) < 0)
            die_errno("Unable to write block '%s'", line);

        total += blocklen;
        close(blockfd);
    }

    if (linelen < 0 && !feof(stdin))
        die_errno("Unable to read index");

    if ((parse_trailer(expected_trailer, &expected_len, line)) < 0)
        die("Unable to read index");

    if (crypto_generichash_final(state, computed_hash, sizeof(computed_hash)) < 0)
        die("Unable to finalize hash");

    if (total != expected_len)
        die("Size mismatch");

    if (memcmp(computed_hash, expected_trailer, sizeof(computed_hash)))
        die("Hash mismatch");

    free(line);
    free(state);
    free(block);

    close(storefd);

    return 0;
}
