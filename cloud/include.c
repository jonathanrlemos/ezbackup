/* include.c
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "include.h"
#include "../cli.h"
#include "../crypt/crypt_getpassword.h"
#include "../readline_include.h"
#include "../options/options.h"
#include "../log.h"
#include "../strings/stringhelper.h"
#include "mega.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

struct cloud_options* co_new(void){
	struct cloud_options* ret;
	ret = calloc(1, sizeof(*ret));
	if (!ret){
		log_enomem();
		return NULL;
	}
	co_set_default_upload_directory(ret);
	return ret;
}

int co_set_username(struct cloud_options* co, const char* username){
	free(co->username);

	if (!username || strlen(username) == 0){
		co->username = NULL;
		return 0;
	}
	co->username = sh_dup(username);
	if (!co->username){
		log_enomem();
		return -1;
	}
	return 0;
}

int co_set_username_stdin(struct cloud_options* co){
	char* tmp;
	int ret;

	tmp = readline("Username:");
	ret = co_set_username(co, tmp);
	free(tmp);

	return ret;
}

int co_set_password(struct cloud_options* co, const char* password){
	if (co->password){
		free(co->password);
	}
	if (!password || strlen(password) == 0){
		co->password = NULL;
		return 0;
	}
	co->password = sh_dup(password);
	if (!co->password){
		log_enomem();
		return -1;
	}
	return 0;
}

int co_set_password_stdin(struct cloud_options* co){
	char* tmp;
	int res;

	do{
		res = crypt_getpassword("Password:", "Verify password:", &tmp);
	}while (res > 0);
	if (res < 0){
		log_error("Error reading password from terminal");
		crypt_freepassword(tmp);
		return -1;
	}

	res = co_set_password(co, tmp);
	crypt_freepassword(tmp);

	return res;
}

int co_set_upload_directory(struct cloud_options* co, const char* upload_directory){
	if (co->upload_directory){
		free(co->upload_directory);
		co->upload_directory = NULL;
	}
	if (!upload_directory){
		co->upload_directory = NULL;
		return 0;
	}
	co->upload_directory = malloc(strlen(upload_directory) + 1);
	if (!co->upload_directory){
		log_enomem();
		return -1;
	}
	strcpy(co->upload_directory, upload_directory);
	return 0;
}

int co_set_default_upload_directory(struct cloud_options* co){
	return co_set_upload_directory(co, "/Backups");
}

int co_set_cp(struct cloud_options* co, enum CLOUD_PROVIDER cp){
	co->cp = cp;
	return 0;
}

enum CLOUD_PROVIDER cloud_provider_from_string(const char* str){
	enum CLOUD_PROVIDER ret = CLOUD_INVALID;
	if (!strcmp(str, "mega") ||
			!strcmp(str, "MEGA") ||
			!strcmp(str, "mega.nz") ||
			!strcmp(str, "mega.co.nz")){
		ret = CLOUD_MEGA;
	}
	else if (!strcmp(str, "none") ||
			!strcmp(str, "off")){
		ret = CLOUD_NONE;
	}
	else{
		log_warning_ex("Invalid --cloud option chosen (%s)", str);
		ret = CLOUD_INVALID;
	}
	return ret;
}

const char* cloud_provider_to_string(enum CLOUD_PROVIDER cp){
	switch (cp){
	case CLOUD_NONE:
		return "none";
	case CLOUD_MEGA:
		return "mega.nz";
	default:
		return "invalid";
	}
}

void co_free(struct cloud_options* co){
	free(co->password);
	free(co->username);
	free(co->upload_directory);
	free(co);
}

int co_cmp(const struct cloud_options* co1, const struct cloud_options* co2){
	if (co1->cp != co2->cp){
		return co1->cp - co2->cp;
	}
	if (sh_cmp_nullsafe(co1->username, co2->username) != 0){
		return sh_cmp_nullsafe(co1->username, co2->username);
	}
	if (sh_cmp_nullsafe(co1->password, co2->password) != 0){
		return sh_cmp_nullsafe(co1->password, co2->password);
	}
	if (sh_cmp_nullsafe(co1->upload_directory, co2->upload_directory) != 0){
		return sh_cmp_nullsafe(co1->upload_directory, co2->upload_directory);
	}
	return 0;
}

char* get_default_out_file(const char* full_path){
	int cwd_len = 256;
	char* cwd;
	const char* filename = sh_filename(full_path);

	cwd = malloc(cwd_len);
	if (!cwd){
		log_enomem();
		return NULL;
	}

	while (getcwd(cwd, cwd_len) == NULL){
		cwd_len *= 2;
		cwd = realloc(cwd, cwd_len);
		if (!cwd){
			log_enomem();
			return NULL;
		}
	}
	cwd = realloc(cwd, strlen(cwd) + 1 + strlen(filename) + 1);
	if (!cwd){
		log_enomem();
		return NULL;
	}

	if (cwd[strlen(cwd) - 1] != '/'){
		strcat(cwd, "/");
	}
	strcat(cwd, filename);
	return cwd;
}

int mega_upload(const char* file, const char* upload_dir, const char* username, const char* password){
	struct string_array* arr = NULL;
	MEGAhandle* mh = NULL;
	int ret = 0;
	size_t i;

	return_ifnull(file, -1);
	return_ifnull(upload_dir, -1);

	if (MEGAlogin(username, password, &mh) != 0){
		log_debug("Failed to log in to MEGA");
		ret = -1;
		goto cleanup;
	}

	if ((arr = sa_get_parent_dirs(upload_dir)) == NULL){
		log_debug("Failed to determine parent directories");
		ret = -1;
		goto cleanup;
	}

	for (i = 0; i < arr->len; ++i){
		if (MEGAmkdir(arr->strings[i], mh) < 0){
			log_debug("Failed to create directory on MEGA");
			ret = -1;
			goto cleanup;
		}
	}

	if (MEGAupload(file, upload_dir, "Uploading file to MEGA", mh) != 0){
		log_debug("Failed to upload file to MEGA");
		ret = -1;
		goto cleanup;
	}

	if (MEGAlogout(mh) != 0){
		log_debug("Failed to logout of MEGA");
		ret = -1;
		mh = NULL;
		goto cleanup;
	}
	mh = NULL;

cleanup:
	if (mh && MEGAlogout(mh) != 0){
		log_debug("Failed to logout of MEGA");
		ret = -1;
	}

	arr ? sa_free(arr) : (void)0;
	return ret;
}

int mega_download(const char* download_dir, const char* out_dir, const char* username, const char* password, char** out_file){
	char* msg = NULL;
	char** files = NULL;
	size_t len = 0;
	MEGAhandle* mh = NULL;
	int res;
	int ret = 0;

	if (MEGAlogin(username, password, &mh) != 0){
		log_debug("Failed to login to MEGA");
		ret = -1;
		goto cleanup;
	}

	if (MEGAreaddir(download_dir, &files, &len, mh) != 0){
		printf("Download directory does not exist\n");
		ret = -1;
		goto cleanup;
	}

	res = time_menu(files, len);
	if (res < 0){
		log_error("Invalid option chosen");
		ret = -1;
		goto cleanup;
	}

	if (!out_dir){
		*out_file = get_default_out_file(files[res]->name);
		if (!(*out_file)){
			log_debug("Failed to determine out file");
			ret = -1;
			goto cleanup;
		}
	}
	else{
		*out_file = malloc(strlen(out_dir) + 1 + strlen(files[res]->name) + 1);
		if (!(*out_file)){
			log_enomem();
			ret = -1;
			goto cleanup;
		}
		strcpy(*out_file, out_dir);
		if ((*out_file)[strlen(*out_file) - 1] != '/'){
			strcat(*out_file, "/");
		}
		strcat(*out_file, sh_filename(files[res]->name));
	}

	msg = malloc(strlen(files[res]->name) + strlen(*out_file) + 64);
	if (!msg){
		log_enomem();
		ret = -1;
		goto cleanup;
	}
	sprintf(msg, "Downloading %s to %s...", files[res]->name, *out_file);
	if (MEGAdownload(files[res]->name, *out_file, msg, mh) != 0){
		log_debug_ex("Failed to download %s", files[res]->name);
		ret = -1;
		goto cleanup;
	}

cleanup:
	if (mh && MEGAlogout(mh) != 0){
		log_debug("Failed to log out of MEGA");
	}
	free_file_nodes(files, len);
	free(msg);

	return ret;
}

int mega_rm(const char* path, const char* username, const char* password){
	MEGAhandle* mh;
	if (MEGAlogin(username, password, &mh) != 0){
		log_debug("Failed to log in to MEGA");
		MEGAlogout(mh);
		return -1;
	}
	if (MEGArm(path, mh) != 0){
		log_debug_ex("Failed to remove %s from MEGA", path);
		MEGAlogout(mh);
		return -1;
	}
	if (MEGAlogout(mh) != 0){
		log_debug("Failed to log out of MEGA");
		return -1;
	}
	return 0;
}

int cloud_upload(const char* in_file, struct cloud_options* co){
	int ret = 0;
	int co_contains_username = co->username != NULL;
	int co_contains_password = co->password != NULL;
	char* user = NULL;
	char* pw = NULL;

	if (!co_contains_username){
		char* tmp = NULL;

		do{
			free(user);
			free(tmp);

			user = readline("Username:");
			if (strlen(user) == 0){
				log_info("Blank username specified");
				ret = 0;
				goto cleanup;
			}

			tmp = readline("Verify  :");
		}while (strcmp(user, tmp) != 0);

		free(tmp);
	}

	if (!co_contains_password){
		int res;
		while ((res = crypt_getpassword("Password:", "Verify  :", &pw)) > 0){
			printf("The passwords do not match.");
			free(pw);
		}

		if (strlen(pw) == 0){
			log_info("Blank password specified");
			ret = 0;
			goto cleanup;
		}
	}

	switch (co->cp){
	case CLOUD_NONE:
		break;
	case CLOUD_MEGA:
		ret = mega_upload(in_file, co->upload_directory, co_contains_username ? co->username : user, co_contains_password ? co->password : pw);
		break;
	default:
		log_error("Invalid CLOUD_PROVIDER passed");
		ret = -1;
		break;
	}

cleanup:
	free(user);
	free(pw);
	return ret;
}

int cloud_download(const char* out_dir, struct cloud_options* co, char** out_file){
	int ret = 0;
	int co_contains_username = co->username != NULL;
	int co_contains_password = co->password != NULL;
	char* user = NULL;
	char* pw = NULL;
	char* cwd = NULL;

	if (!out_dir){
		cwd = sh_getcwd();
	}

	if (!co_contains_username){
		char* tmp = NULL;

		do{
			free(user);
			free(tmp);

			user = readline("Username:");
			if (strlen(user) == 0){
				log_info("Blank username specified");
				ret = 0;
				goto cleanup;
			}

			tmp =  readline("Verify  :");
		}while (strcmp(user, tmp) != 0);

		free(tmp);
	}

	if (!co_contains_password){
		int res;
		while ((res = crypt_getpassword("Password:", "Verify  :", &pw)) > 0){
			printf("The passwords do not match.");
			free(pw);
		}

		if (strlen(pw) == 0){
			log_info("Blank password specified");
			ret = 0;
			goto cleanup;
		}
	}

	switch (co->cp){
	case CLOUD_NONE:
		break;
	case CLOUD_MEGA:
		ret = mega_download(co->upload_directory, cwd ? cwd : out_dir, co_contains_username ? co->username : user, co_contains_password ? co->password : pw, out_file);
		break;
	default:
		log_error("Invalid CLOUD_PROVIDER passed");
		ret = -1;
		break;
	}

cleanup:
	free(user);
	free(pw);
	free(cwd);
	return ret;
}

int cloud_rm(const char* path, struct cloud_options* co){
	int ret = 0;
	int co_contains_username = co->username != NULL;
	int co_contains_password = co->password != NULL;
	char* user = NULL;
	char* pw = NULL;

	if (!co_contains_username){
		char* tmp = NULL;

		do{
			free(user);
			free(tmp);

			user = readline("Username:");
			if (strlen(user) == 0){
				log_info("Blank username specified");
				ret = 0;
				goto cleanup;
			}

			tmp = readline("Verify  :");
		}while (strcmp(user, tmp) != 0);

		free(tmp);
	}

	if (!co_contains_password){
		int res;
		while ((res = crypt_getpassword("Password:", "Verify  :", &pw)) > 0){
			printf("The passwords do not match.");
			free(pw);
		}

		if (strlen(pw) == 0){
			log_info("Blank password specified");
			ret = 0;
			goto cleanup;
		}
	}

	switch (co->cp){
	case CLOUD_NONE:
		break;
	case CLOUD_MEGA:
		ret = mega_rm(path, co_contains_username ? co->username : user, co_contains_password ? co->password : pw);
		break;
	default:
		log_error("Invalid CLOUD_PROVIDER passed");
		ret = -1;
		break;
	}

cleanup:
	free(user);
	free(pw);
	return ret;
}
