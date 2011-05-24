#include <string.h>
#include <err.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"
#include "pkg_util.h"

int
pkg_delete(struct pkg *pkg, struct pkgdb *db, int force)
{
	struct pkg **rdeps;
	int i, ret;
	struct sbuf *rdep_msg;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (db == NULL)
		return (ERROR_BAD_ARG("db"));

	/*
	 * Ensure that we have all the informations we need
	 */
	if ((ret = pkgdb_loadrdeps(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadfiles(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loaddirs(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadscripts(db, pkg)) != EPKG_OK)
		return (ret);
	if ((ret = pkgdb_loadmtree(db, pkg)) != EPKG_OK)
		return (ret);

	rdeps = pkg_rdeps(pkg);

	if (rdeps[0] != NULL) {
		rdep_msg = sbuf_new_auto();
		sbuf_printf(rdep_msg, "%s-%s is required by other packages:", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		for (i = 0;rdeps[i] != NULL; i++) {
			sbuf_cat(rdep_msg, " ");
			sbuf_printf(rdep_msg, "%s-%s", pkg_get(rdeps[i], PKG_NAME), pkg_get(rdeps[i], PKG_VERSION));
		}
		if (!force) {
			sbuf_finish(rdep_msg);
			ret = pkg_error_set(EPKG_REQUIRED, "%s", sbuf_get(rdep_msg));
			sbuf_free(rdep_msg);
			return ret;
		}
		sbuf_cat(rdep_msg, ", deleting anyway");
		sbuf_finish(rdep_msg);
		fprintf(stderr, "%s\n", sbuf_get(rdep_msg));
		sbuf_free(rdep_msg);
	}

	if ((ret = pkg_script_pre_deinstall(pkg)) != EPKG_OK)
		return (ret);

	if ((ret = pkg_delete_files(pkg, force)) != EPKG_OK)
		return (ret);

	if ((ret = pkg_script_post_deinstall(pkg)) != EPKG_OK)
		return (ret);

	if ((ret = pkg_delete_dirs(pkg, force)) != EPKG_OK)
		return (ret);

	return (pkgdb_unregister_pkg(db, pkg_get(pkg, PKG_ORIGIN)));
}

int
pkg_delete_files(struct pkg *pkg, int force)
{
	int i;
	struct pkg_file **files;
	char sha256[65];
	const char *path;

	files = pkg_files(pkg);
	for (i = 0; files[i] != NULL; i++) {
		path = pkg_file_path(files[i]);

		/* Regular files and links */
		/* check sha256 */
		if (!force && pkg_file_sha256(files[i])[0] != '\0') {
			if (sha256_file(path, sha256) == -1) {
				warnx("sha256 calculation failed for '%s'",
					  path);
			} else if (strcmp(sha256, pkg_file_sha256(files[i])) != 0) {
				warnx("%s fails original SHA256 checksum, not removing", path);
				continue;
			}
		}

		if (unlink(path) == -1) {
			warn("unlink(%s)", path);
			continue;
		}
	}

	return (EPKG_OK);
}

int
pkg_delete_dirs(struct pkg *pkg, int force)
{
	int i;
	const char **dirs;

	dirs = pkg_dirs(pkg);
	for (i = 0; dirs[i] != NULL; i++) {
		if (rmdir(dirs[i]) == -1 && errno != ENOTEMPTY && force != 1) {
			warn("rmdir(%s)", dirs[i]);
		}
	}

	return (EPKG_OK);
}
