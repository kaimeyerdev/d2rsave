#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>

static const int name_offset = 0x12b;
static const int seed_offset = 0x9b;
static const int checksum_offset = 0xc;
#define NAME_SIZE 15

uint32_t rol32(uint32_t checksum) {
    return (checksum << 1) | (checksum >> 31);
}

uint32_t calc_checksum(int fd) {
    uint32_t checksum = 0;
    struct stat statbuf;
    int8_t* filebuf = NULL;
    memset(&statbuf, 0, sizeof(statbuf));
    // Calculate checksum
    lseek(fd, 0, SEEK_SET);
    fstat(fd, &statbuf);
    filebuf = malloc(statbuf.st_size);
    read(fd, filebuf, statbuf.st_size);
    for(int i = 0; i < statbuf.st_size; i++) {
        uint8_t b = filebuf[i];
        if (12 <= i && i < 16) {
            b = 0;
        }
        checksum = rol32(checksum) + b;
    }
    return checksum;
}

void read_checksum(int fd, uint32_t *checksum_buf) {
    lseek(fd, checksum_offset, SEEK_SET);
    read(fd, checksum_buf, sizeof(*checksum_buf));
}

void read_name(int fd, char name_buf[NAME_SIZE]) {
    lseek(fd, name_offset, SEEK_SET);
    read(fd, name_buf, NAME_SIZE);
}

void read_seed(int fd, uint32_t *seed_buf) {
    lseek(fd, seed_offset, SEEK_SET);
    read(fd, seed_buf, sizeof(*seed_buf));
}

void write_checksum(int fd, uint32_t checksum) {
    lseek(fd, checksum_offset, SEEK_SET);
    write(fd, &checksum, sizeof(checksum));
}

void write_name(int fd, char name_buf[NAME_SIZE]) {
    lseek(fd, name_offset, SEEK_SET);
    write(fd, name_buf, NAME_SIZE);
    fsync(fd);
}

void write_seed(int fd, uint32_t seed) {
    lseek(fd, seed_offset, SEEK_SET);
    write(fd, &seed, sizeof(seed));
}

int main(int argc, const char** argv)
{
    const char* filename = NULL;
    uint32_t old_seed = 0;
    uint32_t new_seed = 0;
    char old_name[NAME_SIZE];
    char new_name[NAME_SIZE];
    uint32_t old_checksum = 0;
    uint32_t new_checksum = 0;
    char* end = NULL;
    int fd = 0;

    // Init buffers
    memset(old_name, 0, NAME_SIZE);
    memset(new_name, 0, NAME_SIZE);

    if (argc < 4) {
        fprintf(stderr, "Need 3 arguments\n");
        return 1;
    }
    filename = argv[1];
    new_seed = strtoumax(argv[2], &end, 0);
    strncpy(new_name, argv[3], NAME_SIZE);

    // Open file
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open file: [%d] %s\n", errno, filename);
        return errno;
    }

    // Read old checksum
    read_checksum(fd, &old_checksum);
    // Read old name
    read_name(fd, old_name);
    // Read old seed
    read_seed(fd, &old_seed);

    printf("Changing:\n");
    printf("    Seed: %u -> %u\n", old_seed, new_seed);
    printf("    Name: %.*s -> %.*s\n", NAME_SIZE, old_name, NAME_SIZE, new_name);

    if (new_seed != 0) {
        // Write new seed
        write_seed(fd, new_seed);
    }
    if (new_name[0] != '-')
    {
        // Write new name
        write_name(fd, new_name);
    }

    new_checksum = calc_checksum(fd);
    // Write checksum
    write_checksum(fd, new_checksum);

    // Read new checksum
    read_checksum(fd, &new_checksum);
    // Read new name
    read_name(fd, new_name);
    // Read new seed
    read_seed(fd, &new_seed);
    printf("Changed:\n");
    printf("    Checksum: %u -> %u\n", old_checksum, new_checksum);
    printf("    Seed: %u -> %u\n", old_seed, new_seed);
    printf("    Name: %.*s -> %.*s\n", NAME_SIZE, new_name, NAME_SIZE, new_name);

    // Close
    close(fd);

    return 0;
}
