/* backup.c -- backup backend
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "backup.h"
#include "readfile.h"
#include "crypt.h"
#include "fileiterator.h"
#include "error.h"
#include "checksum.h"
#include "options.h"
#include <string.h>
#include <sys/resource.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>

#define UNUSED(x) ((void)x)

struct backup_params{
	TAR*            tp;
	struct TMPFILE* tfp_hashes;
	struct TMPFILE* tfp_hashes_prev;
	struct options* opt;
};

int __coredumps(int enable){
	static struct rlimit rl_prev;
	static int previously_disabled = 0;
	struct rlimit rl;
	int ret = 0;
	if (!enable){
		if (getrlimit(RLIMIT_CORE, &rl_prev) != 0){
			log_warning(__FL__, "Failed to get previous core dump limits");
			ret = -1;
		}
		rl.rlim_cur = 0;
		rl.rlim_max = 0;
		if (setrlimit(RLIMIT_CORE, &rl) != 0){
			log_warning(__FL__, "Failed to disable core dumps");
			ret = -1;
		}
		previously_disabled = 1;
	}
	else{
		if (!previously_disabled){
			return 0;
		}
		if (setrlimit(RLIMIT_CORE, &rl_prev) != 0){
			log_warning(__FL__, "Failed to restore previous core dump limits");
			ret = -1;
		}
		previously_disabled = 0;
	}
	return ret;
}

int disable_core_dumps(void){
	return __coredumps(0);
}

int enable_core_dumps(void){
	return __coredumps(1);
}

int extract_prev_checksums(const char* in, const char* out, const EVP_CIPHER* enc_algorithm, int verbose){
	char pwbuffer[1024];
	struct crypt_keys* fk;
	struct TMPFILE* tfp_decrypt = NULL;
	char prompt[128];
	int ret = 0;

	if (!in || !out || !enc_algorithm){
		log_error(__FL__, STR_ENULL);
		return -1;
	}

	if (disable_core_dumps() != 0){
		log_debug(__FL__, "Did not disable core dumps");
	}

	if ((fk = crypt_new()) == NULL){
		log_debug(__FL__, "Failed to initialzie crypt_keys");
		return -1;
	}

	if (crypt_set_encryption(enc_algorithm, fk) != 0){
		log_debug(__FL__, "crypt_set_encryption() failed");
		ret = -1;
		goto cleanup;
	}

	if (crypt_extract_salt(in, fk) != 0){
		log_debug(__FL__, "crypt_extract_salt() failed");
		ret = -1;
		goto cleanup;
	}

	sprintf(prompt, "Enter %s decryption password", EVP_CIPHER_name(enc_algorithm));
	if ((crypt_getpassword(prompt, NULL, pwbuffer, sizeof(pwbuffer))) != 0){
		log_debug(__FL__, "crypt_getpassword() failed");
		ret = -1;
		goto cleanup;
	}

	if ((crypt_gen_keys((unsigned char*)pwbuffer, strlen(pwbuffer), NULL, 1, fk)) != 0){
		crypt_scrub(pwbuffer, strlen(pwbuffer) + 5 + crypt_randc() % 11);
		log_debug(__FL__, "crypt_gen_keys() failed)");
		ret = -1;
		goto cleanup;
	}
	crypt_scrub(pwbuffer, strlen(pwbuffer) + 5 + crypt_randc() % 11);

	if ((tfp_decrypt = temp_fopen("/var/tmp/decrypt_XXXXXX", "w+b")) == NULL){
		log_debug(__FL__, "temp_file() for file_decrypt failed");
		ret = -1;
		goto cleanup;
	}

	if ((crypt_decrypt_ex(in, fk, out, verbose, "Decrypting file...")) != 0){
		crypt_free(fk);
		log_debug(__FL__, "crypt_decrypt() failed");
		ret = -1;
		goto cleanup;
	}

	if (tar_extract_file(tfp_decrypt->name, "/checksums", out) != 0){
		log_debug(__FL__, "tar_extract_file() failed");
		ret = -1;
		goto cleanup;
	}

	shred_file(tfp_decrypt->name);

cleanup:
	crypt_free(fk);

	if (tfp_decrypt && temp_fclose(tfp_decrypt) != 0){
		log_debug(__FL__, STR_EFCLOSE);
	}

	if (enable_core_dumps() != 0){
		log_debug(__FL__, "enable_core_dumps() failed");
	}
	return ret;
}

int encrypt_file(const char* in, const char* out, const EVP_CIPHER* enc_algorithm, int verbose){
	char pwbuffer[1024];
	struct crypt_keys* fk;
	const EVP_CIPHER* enc;
	char prompt[128];
	int ret;

	/* disable core dumps if possible */
	if (disable_core_dumps() != 0){
		log_warning(__FL__, "Core dumps could not be disabled\n");
	}

	if ((fk = crypt_new()) == NULL){
		log_debug(__FL__, "Failed to generate new struct crypt_keys");
		return -1;
	}

	if (crypt_set_encryption(enc_algorithm, fk) != 0){
		log_debug(__FL__, "Could not set encryption type");
		ret = -1;
		goto cleanup;
	}

	if (crypt_gen_salt(fk) != 0){
		log_debug(__FL__, "Could not generate salt");
		ret = -1;
		goto cleanup;
	}

	sprintf(prompt, "Enter %s encryption password", EVP_CIPHER_name(enc));
	/* PASSWORD IN MEMORY */
	while ((ret = crypt_getpassword(prompt,
					"Verify encryption password",
					pwbuffer,
					sizeof(pwbuffer))) > 0){
		printf("\nPasswords do not match\n");
	}
	if (ret < 0){
		log_debug(__FL__, "crypt_getpassword() failed");
		crypt_scrub(pwbuffer, strlen(pwbuffer) + 5 + crypt_randc() % 11);
		ret = -1;
		goto cleanup;
	}

	if ((crypt_gen_keys((unsigned char*)pwbuffer, strlen(pwbuffer), NULL, 1, fk)) != 0){
		crypt_scrub(pwbuffer, strlen(pwbuffer) + 5 + crypt_randc() % 11);
		log_debug(__FL__, "crypt_gen_keys() failed");
		ret = -1;
		goto cleanup;
	}
	/* don't need to scrub entire buffer, just where the password was
	 * and a little more so attackers don't know how long the password was */
	crypt_scrub((unsigned char*)pwbuffer, strlen(pwbuffer) + 5 + crypt_randc() % 11);
	/* PASSWORD OUT OF MEMORY */

	if ((crypt_encrypt_ex(in, fk, out, verbose, "Encrypting file...")) != 0){
		crypt_free(fk);
		log_debug(__FL__, "crypt_encrypt() failed");
		ret = -1;
		goto cleanup;
	}

cleanup:
	/* shreds keys as well */
	crypt_free(fk);
	if (enable_core_dumps() != 0){
		log_debug(__FL__, "enable_core_dumps() failed");
	}
	return 0;
}

int rename_ex(const char* _old, const char* _new){
	FILE* fp_old;
	FILE* fp_new;
	unsigned char buffer[BUFFER_LEN];
	int len;

	if (rename(_old, _new) == 0){
		return 0;
	}

	fp_old = fopen(_old, "rb");
	if (!fp_old){
		log_error(__FL__, STR_EFOPEN, _old, strerror(errno));
		return -1;
	}

	fp_new = fopen(_new, "wb");
	if (!fp_new){
		log_error(__FL__, STR_EFOPEN, _new, strerror(errno));
		fclose(fp_old);
		return -1;
	}

	while ((len = read_file(fp_old, buffer, sizeof(buffer))) > 0){
		fwrite(buffer, 1, len, fp_new);
		if (ferror(fp_new)){
			fclose(fp_old);
			fclose(fp_new);
			log_error(__FL__, STR_EFWRITE, _new);
			return -1;
		}
	}

	fclose(fp_old);

	if (fclose(fp_new) != 0){
		log_error(__FL__, STR_EFCLOSE, _old);
		return -1;
	}

	remove(_old);
	return 0;
}

int func(const char* file, const char* dir, struct stat* st, void* params){
	struct backup_params* bparams = (struct backup_params*)params;
	char* path_in_tar;
	int err;
	int i;
	UNUSED(st);

	/* exclude lost+found */
	if (strlen(dir) > strlen("lost+found") &&
			!strcmp(dir + strlen(dir) - strlen("lost+found"), "lost+found")){
		return 0;
	}
	/* exclude in exclude list */
	for (i = 0; i < bparams->opt->exclude_len; ++i){
		/* stop iterating through this directory */
		if (!strcmp(dir, bparams->opt->exclude[i])){
			return 0;
		}
	}

	err = add_checksum_to_file(file, bparams->opt->hash_algorithm, bparams->tfp_hashes->fp, bparams->tfp_hashes_prev ? bparams->tfp_hashes_prev->fp : NULL);
	if (err == 1){

		if (bparams->opt->flags & FLAG_VERBOSE){
			printf("Skipping unchanged (%s)\n", file);
		}

		return 1;
	}
	else if (err != 0){
		log_debug(__FL__, "add_checksum_to_file() failed");
	}

	path_in_tar = malloc(strlen(file) + sizeof("/files"));
	if (!path_in_tar){
		log_fatal(__FL__, STR_ENOMEM);
		return 0;
	}
	strcpy(path_in_tar, "/files");
	strcat(path_in_tar, file);

	if (tar_add_file_ex(bparams->tp, file, path_in_tar, bparams->opt->flags & FLAG_VERBOSE, file) != 0){
		log_debug(__FL__, "Failed to add file to tar");
	}
	free(path_in_tar);

	return 1;
}

int error(const char* file, int __errno, void* params){
	UNUSED(params);
	fprintf(stderr, "%s: %s\n", file, strerror(__errno));

	return 1;
}

int get_default_backup_name(struct options* opt, char** out){
	char file[128];

	/* /home/<user>/Backups/backup-<unixtime>.tar(.bz2)(.crypt) */
	*out = malloc(strlen(opt->output_directory) + sizeof(file));
	if (!(out)){
		log_error(__FL__, STR_ENOMEM);
		return -1;
	}

	/* get unix time and concatenate it to the filename */
	sprintf(file, "/backup-%ld", (long)time(NULL));
	strcpy(*out, opt->output_directory);
	strcat(*out, file);

	/* concatenate extensions */
	strcat(*out, ".tar");
	switch (opt->comp_algorithm){
	case COMPRESSOR_GZIP:
		strcat(*out, ".gz");
		break;
	case COMPRESSOR_BZIP2:
		strcat(*out, ".bz2");
		break;
	case COMPRESSOR_XZ:
		strcat(*out, ".xz");
		break;
	case COMPRESSOR_LZ4:
		strcat(*out, ".lz4");
		break;
	default:
		;
	}
	if (opt->enc_algorithm){
		char enc_algo_str[64];
		sprintf(enc_algo_str, ".%s", EVP_CIPHER_name(opt->enc_algorithm));
		strcat(*out, enc_algo_str);
	}
	return 0;
}

int add_default_directories(struct options* opt){
	struct passwd* pw;
	const char* homedir;

	if (!(homedir = getenv("HOME"))){
		pw = getpwuid(getuid());
		if (!pw){
			log_error(__FL__, "Failed to get home directory");
			return -1;
		}
		homedir = pw->pw_dir;
	}

	if (opt->directories){
		int i;
		for (i = 0; i < opt->directories_len; ++i){
			free(opt->directories[i]);
		}
		free(opt->directories);
	}

	opt->directories_len = 1;
	opt->directories = malloc(sizeof(*(opt->directories)));
	if (!opt->directories){
		log_fatal(__FL__, STR_ENOMEM);
		return -1;
	}
	opt->directories[0] = malloc(strlen(homedir) + 1);
	if (!opt->directories[0]){
		log_fatal(__FL__, STR_ENOMEM);
		free(opt->directories);
		return -1;
	}

	strcpy(opt->directories[0], homedir);
	return 0;
}

int backup(struct options* opt, const struct options* opt_prev){
	struct backup_params bp;
	struct TMPFILE* tfp_tar = NULL;
	struct TMPFILE* tfp_sorted = NULL;
	struct TMPFILE* tfp_removed = NULL;
	struct TMPFILE* tfp_config_prev = NULL;
	char* file_out = NULL;
	int ret = 0;
	int i;

	memset(&bp, 0, sizeof(bp));
	bp.opt = opt;

	if (opt->directories_len <= 0 &&
			add_default_directories(opt) != 0){
		log_error(__FL__, "Failed to determine directories");
		return -1;
	}

	/* load previous hash file */
	if (opt_prev &&
			opt_prev->prev_backup &&
			opt_prev->hash_algorithm == bp.opt->hash_algorithm){
		bp.tfp_hashes_prev = temp_fopen("/var/tmp/prev_XXXXXX", "w+b");
		if (!bp.tfp_hashes_prev){
			log_debug(__FL__, "Failed to create temp file for previous backup");
		}

		if (bp.tfp_hashes_prev && extract_prev_checksums(opt_prev->prev_backup, bp.tfp_hashes_prev->name, opt_prev->enc_algorithm, opt->flags & FLAG_VERBOSE) != 0){
			log_debug(__FL__, "Failed to extract previous checksums");
			temp_fclose(bp.tfp_hashes_prev);
			bp.tfp_hashes_prev = NULL;
		}
	}

	if (get_default_backup_name(opt, &file_out) != 0){
		log_debug(__FL__, "Failed to generate backup name");
		ret = -1;
		goto cleanup;
	}

	if ((tfp_tar = temp_fopen("/var/tmp/tar_XXXXXX", "wb")) == NULL){
		log_debug(__FL__, "Failed to generate temp file for tar");
		ret = -1;
		goto cleanup;
	}

	printf("Adding files to %s...\n", file_out);
	bp.tp = tar_create(tfp_tar->name, bp.opt->comp_algorithm, bp.opt->comp_level);
	if (!bp.tp){
		log_debug(__FL__, "Failed to create tar");
		ret = -1;
		goto cleanup;
	}

	bp.tfp_hashes = temp_fopen("/var/tmp/hashes_XXXXXX", "w+b");
	if (!bp.tfp_hashes){
		log_debug(__FL__, "Failed to create temp file for hashes");
		ret = -1;
		goto cleanup;
	}

	for (i = 0; i < bp.opt->directories_len; ++i){
		enum_files(bp.opt->directories[i], func, &bp, error, NULL);
	}
	temp_fclose(bp.tfp_hashes_prev);

	if ((tfp_sorted = temp_fopen("/var/tmp/sorted_XXXXXX", "wb")) == NULL){
		log_warning(__FL__, "Failed to create temp file for sorted checksum list");
		if (tar_add_file_ex(bp.tp, bp.tfp_hashes->name, "/checksums", bp.opt->flags & FLAG_VERBOSE, "Adding unsorted checksum list") != 0){
			log_warning(__FL__, "Failed to write checksums to backup");
		}
	}
	else{
		if (sort_checksum_file(bp.tfp_hashes->fp, tfp_sorted->fp) != 0){
			log_warning(__FL__, "Failed to sort checksum list");
		}
		if (tar_add_file_ex(bp.tp, tfp_sorted->name, "/checksums", bp.opt->flags & FLAG_VERBOSE, "Adding checksum list...") != 0){
			log_warning(__FL__, "Failed to write checksums to file");
		}
	}

	tfp_removed = temp_fopen("/var/tmp/removed_XXXXXX", "wb");
	if (!tfp_removed){
		log_debug(__FL__, "Failed to created removed temp file");
	}
	else{
		if (create_removed_list(bp.tfp_hashes->fp, tfp_removed->fp) != 0){
			log_debug(__FL__, "Failed to create removed list");
			temp_fclose(tfp_removed);
			tfp_removed = NULL;
		}
	}
	temp_fclose(bp.tfp_hashes);

	if (tfp_removed && tar_add_file_ex(bp.tp, tfp_removed->name, "/removed", bp.opt->flags & FLAG_VERBOSE, "Adding removed list...") != 0){
		log_warning(__FL__, "Failed to add removed list to backup");
	}
	temp_fclose(tfp_removed);

	if (tar_close(bp.tp) != 0){
		log_warning(__FL__, "Failed to close tar. Data corruption possible");
	}

	if (bp.opt->enc_algorithm){
		if (encrypt_file(tfp_tar->name, file_out, bp.opt->enc_algorithm, bp.opt->flags & FLAG_VERBOSE) != 0){
			log_warning(__FL__, "Failed to encrypt file");
		}
	}
	else{
		if (rename_ex(tfp_tar->name, file_out) != 0){
			log_warning(__FL__, "Failed to create destination file");
		}
	}

	if (write_config_file(bp.opt) != 0){
		log_warning(__FL__, "Failed to write config file");
	}

cleanup:
	free(file_out);
	bp.tp ? tar_close(bp.tp) : 0;
	tfp_tar ? temp_fclose(tfp_tar) : 0;
	bp.tfp_hashes ? temp_fclose(bp.tfp_hashes) : 0;
	bp.tfp_hashes_prev ? temp_fclose(bp.tfp_hashes_prev) : 0;

	return ret;
}