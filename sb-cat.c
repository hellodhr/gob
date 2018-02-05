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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sodium.h>

#include "config.h"
#include "common.h"

#define MIN(x, y) ((x) <= (y) ? (x) : (y))

static const char *find_last_line(const char *string)
{
    const char *end = string + strlen(string) - 1;

    while (*end == '\n')
        end--;

    while (end[-1] != '\n' && end > string)
        end--;

    if (end == string)
        return NULL;

    return end;
}

static int parse_trailer(unsigned char *hash_out, size_t *datalen_out, const char *chain)
{
    char hash[HASH_LEN * 2];
    const char *trailer;
    int i;

    if ((trailer = find_last_line(chain)) == NULL)
        die("Invalid input without trailer");

    if (*trailer != '>')
        die("Last line is not a trailer line");
    trailer++;

    for (i = 0; i < (HASH_LEN * 2); i++) {
        char h = *trailer++;
        if (!strchr("0123456789abcdef", h))
            die("Invalid trailer hash");
        hash[i] = h;
    }

    if (*trailer != ' ')
        die("No separator between trailer hash and length");

    if ((*datalen_out = strtol(trailer, NULL, 10)) == 0)
        die("Invalid data length in trailer");

    if (hex2bin(hash_out, HASH_LEN, hash, sizeof(hash)) < 0)
        die("Unable to decode trailer hash");

    return 0;
}

static int read_block(struct block *out, int dirfd, char *hash)
{
    char shard[3];
    int fd, shardfd;

    shard[0] = hash[0];
    shard[1] = hash[1];
    shard[2] = '\0';

    if ((shardfd = openat(dirfd, shard, O_DIRECTORY)) < 0)
        die("Unable to open sharding directory '%s': %s", shard, strerror(errno));

    if ((fd = openat(shardfd, hash + 2, O_RDONLY)) < 0)
        die("Unable to open block '%s': %s", hash, strerror(errno));

    if (read_bytes(fd, (char *) out->data, BLOCK_LEN) != BLOCK_LEN)
        die("Unable to read block '%s': %s", hash, strerror(errno));

    close(shardfd);
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    crypto_generichash_state *state = malloc(crypto_generichash_statebytes());
    unsigned char trailer_hash[HASH_LEN], computed_hash[HASH_LEN];
    char *chain = NULL, *haystack, *hash;
    char buf[1024];
    size_t total = 0, data_len;
    ssize_t bytes;
    int dirfd;

    if (argc != 2)
        die("USAGE: %s <DIR>", argv[0]);

    if ((dirfd = open(argv[1], O_DIRECTORY)) < 0)
        die("Unable to open storage '%s': %s", argv[1], strerror(errno));

    while ((bytes = read_bytes(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        chain = realloc(chain, total + bytes + 1);
        memcpy(chain + total, buf, bytes);
        total += bytes;
        chain[total] = '\0';
    }

    if (bytes < 0)
        die("Unable to read from stdin: %s", strerror(errno));

    if (parse_trailer(trailer_hash, &data_len, chain) < 0)
        die("Unable to parse trailer");

    if (crypto_generichash_init(state, NULL, 0, HASH_LEN) < 0)
        die("Unable to initialize hashing state");

    haystack = chain;
    while ((hash = strtok(haystack, "\n")) != NULL) {
        unsigned char line_hash[HASH_LEN];
        struct block block;
        size_t blocklen = MIN(data_len, BLOCK_LEN);

        if (*hash == '>')
            break;

        if (data_len == 0)
            die("More lines, but all data read");

        if (hex2bin(line_hash, sizeof(line_hash), hash, strlen(hash)) < 0)
            die("Unable to decode hash");

        if (read_block(&block, dirfd, hash) < 0)
            die("Unable to read block '%s': %s", hash, strerror(errno));

        if (crypto_generichash_update(state, block.data, BLOCK_LEN) < 0)
            die("Unable to update hash");

        if (write_bytes(STDOUT_FILENO, (char *) block.data, blocklen) < 0)
            die("Unable to write block '%s': %s", hash, strerror(errno));

        data_len -= blocklen;
        haystack = NULL;
    }

    if (data_len)
        die("Premature end of chain");

    if (crypto_generichash_final(state, computed_hash, sizeof(computed_hash)) < 0)
        die("Unable to finalize hash");

    if (memcmp(computed_hash, trailer_hash, sizeof(computed_hash)))
        die("Trailer hash does not match computed hash");

    free(state);

    return 0;
}
