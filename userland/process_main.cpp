#include <stdint.h>
#include <stddef.h>
#include <cloudlibc/src/include/elf.h>
#include <cloudlibc/src/include/link.h>
#include <cloudabi/headers/cloudabi_types.h>

extern "C"
int getpid();

extern "C"
void putstring(const char*, unsigned int len);

extern "C"
int getchar(int fd, int offset);

extern "C"
int openat(int fd, const char*, int directory);

int (*get_vdso_int)();
const char * (*get_vdso_text)();

size_t
strlen(const char* str) {
	size_t ret = 0;
	while ( str[ret] != 0 )
		ret++;
	return ret;
}

int
strcmp(const char *left, const char *right) {
	while(1) {
		if(*left == 0 && *right == 0) return 0;
		if(*left < *right) return -1;
		if(*left > *right) return 1;
		left++;
		right++;
	}
}

void putstring(const char *buf) {
	putstring(buf, strlen(buf));
}

char *ui64toa_s(uint64_t value, char *buffer, size_t bufsize, int base) {
	static const char xlat[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	if(buffer == NULL || bufsize == 0 || base == 0 || base > 36) {
		return NULL;
	}
	size_t i = bufsize;
	buffer[--i] = 0;
	do {
		if(i == 0) {
			return NULL;
		}

		buffer[--i] = xlat[value % base];
	} while(value /= base);

	return buffer + i;
}

char *i64toa_s(int64_t value, char *buffer, size_t bufsize, int base) {
	uint8_t neg = value < 0;
	if(neg) value = -value;
	char *b = ui64toa_s((uint64_t)value, buffer, bufsize, base);
	if(b == NULL || (b == buffer && neg)) {
		return NULL;
	}
	if(neg) {
		b -= 1;
		b[0] = '-';
	}
	return b;
}

static void link_vdso(const ElfW(Ehdr) * ehdr) {
  // Extract the Dynamic Section of the vDSO.
  const char *base = (const char *)ehdr;
  const ElfW(Phdr) *phdr = (const ElfW(Phdr) *)(base + ehdr->e_phoff);
  size_t phnum = ehdr->e_phnum;
  for (;;) {
    if (phnum == 0)
      return;
    if (phdr->p_type == PT_DYNAMIC)
      break;
    phnum--;
    phdr++;
  }

  // Extract the symbol and string tables.
  const ElfW(Dyn) *dyn = (const ElfW(Dyn) *)(base + phdr->p_offset);
  const char *str = NULL;
  const ElfW(Sym) *sym = NULL;
  size_t symsz = 0;
  while (dyn->d_tag != DT_NULL) {
    switch (dyn->d_tag) {
      case DT_HASH:
        // Number of symbols in the symbol table can only be extracted
        // by fetching the number of chains in the symbol hash table.
        symsz = ((const Elf32_Word *)(base + dyn->d_un.d_ptr))[1];
        break;
      case DT_STRTAB:
        str = base + dyn->d_un.d_ptr;
        break;
      case DT_SYMTAB:
        sym = (const ElfW(Sym) *)(base + dyn->d_un.d_ptr);
        break;
    }
    ++dyn;
  }

  // Scan through all of the symbols and find the implementations of the
  // system calls.
  while (symsz-- > 0) {
    if (ELFW(ST_BIND)(sym->st_info) == STB_GLOBAL &&
        ELFW(ST_TYPE)(sym->st_info) == STT_FUNC &&
        ELFW(ST_VISIBILITY)(sym->st_other) == STV_DEFAULT && sym->st_name > 0) {
      const char *name = str + sym->st_name;
      if (strcmp(name, "get_vdso_int") == 0) {
        get_vdso_int = (decltype(get_vdso_int))(base + sym->st_value);
      } else if(strcmp(name, "get_vdso_text") == 0) {
        get_vdso_text = (decltype(get_vdso_text))(base + sym->st_value);
      } else {
        putstring("Saw unknown symbol named \"");
        putstring(name);
        putstring("\"\n");
      }
    }
    ++sym;
  }
}

extern "C"
void _start(const cloudabi_auxv_t *auxv) {
	putstring("Hello ");
	putstring("world!\n");

	int pid = getpid();
	char buf[100];
	putstring("This is process ");
	putstring(ui64toa_s(pid, buf, sizeof(buf), 10));
	putstring("!\n");

	size_t len;
	for(len = 0; len < sizeof(buf); ++len) {
		int c = getchar(1, len);
		if(c <= 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf, len);

	/* Read procfs/kernel/uptime */
	int c_ = getchar(2, 0);
	putstring("Got return value of procfs getchar:");
	putstring(i64toa_s(c_, buf, sizeof(buf), 10));
	putstring(".\n");

	int dirfd = openat(2, "kernel", 1);
	putstring("openat(2, \"kernel\", 1) = ");
	putstring(i64toa_s(dirfd, buf, sizeof(buf), 10));
	putstring("\n");

	int fd = openat(2, "kernel", 0);
	putstring("openat(2, \"kernel\", 0) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	fd = openat(2, "kernel/uptime", 1);
	putstring("openat(2, \"kernel/uptime\", 1) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	fd = openat(2, "kernel/uptime", 0);
	putstring("openat(2, \"kernel/uptime\", 0) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	putstring("Contents of fd(2)/kernel/uptime: ");
	len = 0;
	for(len = 0; len < sizeof(buf); ++len) {
		int c = getchar(fd, len);
		if(c < 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf);
	putstring("\n");

	fd = openat(dirfd, "uptime", 0);
	putstring("openat(dirfd, \"uptime\", 0) = ");
	putstring(i64toa_s(fd, buf, sizeof(buf), 10));
	putstring("\n");

	putstring("Contents of fd(dirfd)/uptime: ");
	len = 0;
	for(len = 0; len < sizeof(buf); ++len) {
		int c = getchar(fd, len);
		if(c < 0) {
			break;
		}
		buf[len] = c;
	}
	buf[len] = 0;
	putstring(buf);
	putstring("\n");

	if(auxv == 0) {
		putstring("No auxv found, this is required!\n");
		while(1) {}
	} else {
		get_vdso_int = 0;
		get_vdso_text = 0;

		const ElfW(Ehdr) *at_sysinfo_ehdr = NULL;
		for(; auxv->a_type != CLOUDABI_AT_NULL; ++auxv) {
			if(auxv->a_type == CLOUDABI_AT_SYSINFO_EHDR) {
				at_sysinfo_ehdr = (ElfW(Ehdr)*)auxv->a_ptr;
			}
		}
		if(at_sysinfo_ehdr) {
			link_vdso(at_sysinfo_ehdr);
		}
	}

	if(get_vdso_int != nullptr) {
		putstring("VDSO int: ");
		putstring(i64toa_s(get_vdso_int(), buf, sizeof(buf), 10));
		putstring("\n");
	}
	if(get_vdso_text != nullptr) {
		putstring("VDSO text: ");
		putstring(get_vdso_text());
		putstring("\n");
	}

	putstring("Binary done!\n");

	while(1) {}
}
