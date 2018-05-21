#include <cosix/util.hpp>

#include <argdata.h>
#include <errno.h>
#include <program.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <unistd.h>

int stdout = -1;
int blockdevstore = -1;
std::string blockdev;

struct mbr_entry {
	uint8_t status;
	char start_head;
	char start_sector_cylinder2;
	char start_cylinder1;
	uint8_t type;
	char last_head;
	char last_sector_cylinder2;
	char last_cylinder1;
	uint32_t lba_start;
	uint32_t sectorcount;
};

struct mbr_t {
	char bootcode[446];
	mbr_entry partitions[4];
	char signature[2];
} __attribute__((packed));

static_assert(sizeof(mbr_entry) == 16, "Size of MBR entry");
static_assert(sizeof(mbr_t) == 512, "Size of MBR");

static int get_blockdev(std::string name) {
	std::string command = "FD " + name;
	write(blockdevstore, command.c_str(), command.size());
	char buf[20];
	int fdnum;
	if(cosix::read_response_and_fd(blockdevstore, buf, sizeof(buf), fdnum) != 2
	|| strncmp(buf, "OK", 2) != 0) {
		perror("Failed to retrieve blockdev device from blockdevstore");
		return -1;
	}
	return fdnum;
}

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_get(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			argdata_map_next(&it);
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "blockdevstore") == 0) {
			argdata_get_fd(value, &blockdevstore);
		} else if(strcmp(keystr, "blockdev") == 0) {
			const char *blockdevname = nullptr;
			size_t blockdevsz = 0;
			argdata_get_str(value, &blockdevname, &blockdevsz);
			if(blockdevname && blockdevsz) {
				blockdev = std::string(blockdevname, blockdevsz);
			}
		}
		argdata_map_next(&it);
	}

	// reconfigure stderr
	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	if(blockdev.empty() || blockdev.size() >= 64) {
		fprintf(stderr, "[partition] failed to start: blockdev name incorrect\n");
		exit(1);
	}

	int fd = get_blockdev(blockdev);
	mbr_t mbr;
	ssize_t res = pread(fd, &mbr, sizeof(mbr), 0);
	if(res < 0) {
		perror("[partition] failed to read MBR");
		exit(1);
	}

	if(memcmp(mbr.signature, "\x55\xaa", 2) != 0) {
		dprintf(stdout, "[partition] failed to parse MBR: signature incorrect\n");
		exit(1);
	}

	for(size_t p = 0; p < 4; ++p) {
		// TODO: remove partition with name blockdev + 'p' + partition number,
		// unless its properties are exactly the same as what we'll create here

		auto const &partition = mbr.partitions[p];
		if(partition.status != 0x00 /* valid */ && partition.status != 0x80 /* bootable */) {
			// not an actual partition entry
			continue;
		}

		if(partition.type == 0x00) {
			// free space, not a partition
			continue;
		}

		// TODO: 0xED / 0xEE: read GPT?
		// TODO: 0x0F / 0x85: read extended partition table?

		// Create partition
		// For a block device called ata0, this will create partitions ata0p0, ata0p1, ...
		// TODO: an MBR with 1 partition in the last entry in MBR will be called ata0p0, but
		// wouldn't we expect it to be called ata0p3 in that case?
		std::string command = "PARTITION " + blockdev + " " + std::to_string(partition.lba_start)
			+ " " + std::to_string(partition.sectorcount);
		write(blockdevstore, command.c_str(), command.size());
		char buf[64];
		int fdnum;
		ssize_t size = cosix::read_response_and_fd(blockdevstore, buf, sizeof(buf), fdnum);
		if(size < 0) {
			perror("[partition] failed to create partition");
			exit(1);
		}
		if(fdnum >= 0) {
			// we don't need the partition actually
			close(fdnum);
		}
		size_t s_size = size;
		buf[s_size >= sizeof(buf) ? sizeof(buf) - 1 : s_size] = 0;

		std::string unit;
		uint64_t bytes = uint64_t(partition.sectorcount) * 512; // TODO: sector size assumption
		if(bytes >= 10UL * 1024 * 1024 * 1024) {
			bytes /= 1024 * 1024 * 1024;
			unit = "GiB";
		} else if(bytes >= 10 * 1024 * 1024) {
			bytes /= 1024 * 1024;
			unit = "MiB";
		} else if(bytes >= 10 * 1024) {
			bytes /= 1024;
			unit = "KiB";
		} else {
			unit = "B";
		}
		dprintf(stdout, "[partition] created partition: %s (%lld %s)\n", buf, bytes, unit.c_str());
	}

	close(fd);
	exit(0);
}
