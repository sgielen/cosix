#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

int stdout;
int tmpdir;

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "tmpdir") == 0) {
			argdata_get_fd(value, &tmpdir);
		}
	}

	dprintf(stdout, "tmptest spawned, tmpdir is %d\n", tmpdir);

	/* create file */
	const char *filename = "foobar";
	int tmpfile = openat(tmpdir, filename, O_WRONLY | O_CREAT | O_TRUNC);
	if(tmpfile < 0) {
		dprintf(stdout, "Failed to open \"%s\": %s\n", filename, strerror(errno));
		exit(1);
	} else {
		dprintf(stdout, "Opened \"%s\" as %d\n", filename, tmpfile);
	}
	const char *str = "Hello World!";
	ssize_t written = write(tmpfile, str, strlen(str));
	close(tmpfile);

	dprintf(stdout, "Wrote %zu bytes to \"%s\".\n", written, filename);

	/* read file */
	tmpfile = openat(tmpdir, filename, O_RDONLY);
	if(tmpfile < 0) {
		dprintf(stdout, "Failed to reopen \"%s\": %s\n", filename, strerror(errno));
		exit(1);
	}
	char buf[100];
	ssize_t readres = read(tmpfile, buf, sizeof(buf));
	buf[readres] = 0;
	dprintf(stdout, "Read %zu bytes from \"%s\": \"%s\"\n", readres, filename, buf);
	close(tmpfile);

	/* create directory */
	const char *dirname = "somedir";
	if(mkdirat(tmpdir, dirname, 0) < 0) {
		dprintf(stdout, "Failed to create directory \"%s\": %s\n", dirname, strerror(errno));
		exit(1);
	}
	DIR *dir = opendirat(tmpdir, dirname);
	if(dir == 0) {
		dprintf(stdout, "Failed to opendirat: %s\n", strerror(errno));
		exit(1);
	}
	dprintf(stdout, "Contents of %s:\n", dirname);
	struct dirent *ent;
	while((ent = readdir(dir)) != nullptr) {
		dprintf(stdout, "- \"%s\" (inode %llu, type %d)\n", ent->d_name, ent->d_ino, ent->d_type);
	}

	/* move file */
	dprintf(stdout, "Moving file to dir\n");
	if(renameat(tmpdir, filename, dirfd(dir), filename) < 0) {
		dprintf(stdout, "Failed to renameat() tmpfile: %s\n", strerror(errno));
		exit(1);
	}
	closedir(dir);

	/* re-read file */
	char *full_filename;
	asprintf(&full_filename, "%s/%s", dirname, filename);
	dprintf(stdout, "Reopening file \"%s\"", full_filename);
	tmpfile = openat(tmpdir, full_filename, O_RDONLY);
	if(tmpfile < 0) {
		dprintf(stdout, "Failed to reopen \"%s\": %s\n", full_filename, strerror(errno));
		exit(1);
	}
	readres = read(tmpfile, buf, sizeof(buf));
	buf[readres] = 0;
	dprintf(stdout, "Read %zu bytes from \"%s\": \"%s\"\n", readres, full_filename, buf);
	close(tmpfile);

	/* read directory */
	rewinddir(dir);
	struct dirent *dp;
	int files = 0;
	dprintf(stdout, "Reading files from \"%s\"...\n", dirname);
	while((dp = readdir(dir)) != NULL) {
		dprintf(stdout, "%d: %s\n", ++files, dp->d_name);
	}
	dprintf(stdout, "%d files read.\n", files);
	closedir(dir);

	/* remove file */
	if(unlinkat(tmpdir, full_filename, 0) < 0) {
		dprintf(stdout, "Failed to unlink \"%s\": %s\n", full_filename, strerror(errno));
		exit(1);
	}

	exit(0);
}
