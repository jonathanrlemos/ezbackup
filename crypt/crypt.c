/** @file crypt/crypt.c
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "crypt.h"
#include "../filehelper.h"
#include "../log.h"
#include <errno.h>
#include <openssl/err.h>
#include "../progressbar.h"
#include "../strings/stringhelper.h"
#include <termios.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#if (CHAR_BIT != 8)
#error "CHAR_BIT must be 8"
#endif

struct crypt_keys{
	unsigned char* key;              /**< Holds the encryption key. This is not the password, but a value derived from the password. The password cannot be derived from the key. */
	int key_length;                  /**< The length of the key in bytes. */
	unsigned char* iv;               /**< Holds the initialization vector if applicable. */
	int iv_length;                   /**< The length of the initialization vector in bytes. */
	unsigned char salt[8];           /**< Holds a 64-bit salt to make sure the same data does not encrypt to the same value. The salt must be 64-bit to preserve compatibillity with the openssl command line utility. */
	const EVP_CIPHER* encryption;    /**< The encryption algorithm to use. */
	unsigned flag_encryption_set: 1; /**< 0 if the encryption algorithm was not set, 1 if it was. DO NOT EDIT MANUALLY. */
	unsigned flag_keys_set: 1;       /**< 0 if the keys were not generated, 1 if they were. DO NOT EDIT MANUALLY. */
	unsigned flag_salt_extracted: 1; /**< 0 if the salt was not extracted from the file. 1 if it was. DO NOT EDIT MANUALLY. */
};

struct crypt_keys* crypt_new(void){
	struct crypt_keys* ret;

	ret = calloc(1, sizeof(*ret));
	if (!ret){
		log_enomem();
		return NULL;
	}

	return ret;
}

int crypt_scrub(void* data, int len){
	int res;

	return_ifnull(data, -1);

	res = RAND_bytes(data, len);
	if (res != 1){
		/* generate low grade non-cs-secure random numbers
		 * this is so we can return something in case
		 * RAND_bytes() fails
		 *
		 * in case we cannot open /dev/urandom, abort
		 *
		 * DO NOT TRUST FOR CRYPTOGRAPHIC APPLICATIONS */
		FILE* fp;
		int i;
		volatile unsigned char* data_vol = data;

		fp = fopen("/dev/urandom", "rb");
		if (!fp){
			log_error_ex("Could not generate cryptographically secure numbers, and could not open /dev/urandom for reading (%s)", strerror(errno));
			return -1;
		}

		for (i = 0; i < len; ++i){
			data_vol[i] = fgetc(fp);
		}
		fclose(fp);
		log_warning("Somewhat low-grade random data was generated.");
		return 1;
	}
	return 0;
}

unsigned char crypt_randc(void){
	unsigned char ret;
	int res;

	res = RAND_bytes(&ret, 1);
	if (res != 1){
		FILE* fp = fopen("/dev/urandom", "rb");
		if (!fp){
			log_warning("Extremely low-grade random data used in crypt_randc()");
			return random() % 256;
		}
		ret = fgetc(fp);
		fclose(fp);
		log_warning("Somewhat low-grade random data used in crypt_randc()");
		return ret;
	}
	return ret;
}

int crypt_gen_salt(struct crypt_keys* fk){
	return_ifnull(fk, -1);
	return gen_csrand(fk->salt, sizeof(fk->salt));
}

int crypt_set_salt(const unsigned char salt[8], struct crypt_keys* fk){
	unsigned i;

	return_ifnull(fk, -1);

	if (salt){
		/* memcpy leaves data in memory
		 * bad for crypto applications */
		for (i = 0; i < sizeof(fk->salt); ++i){
			fk->salt[i] = salt[i];
		}
	}
	/* if salt is NULL, set salt equal to all zeroes */
	else{
		for (i = 0; i < sizeof(fk->salt); ++i){
			fk->salt[i] = '\0';
		}
	}
	return 0;
}

const EVP_CIPHER* crypt_get_cipher(const char* encryption_name){
	const EVP_CIPHER* ret = NULL;
	const EVP_CIPHER* enc_null = EVP_enc_null();

	if (!encryption_name){
		return NULL;
	}
	if (sh_ncasecmp(encryption_name, "none") == 0 || sh_ncasecmp(encryption_name, "null") == 0){
		return EVP_enc_null();
	}
	ret = EVP_get_cipherbyname(encryption_name);
	if (sh_ncasecmp(EVP_CIPHER_name(enc_null), EVP_CIPHER_name(ret)) == 0){
		return NULL;
	}
	return ret;
}

/* sets encryption type, this must be the first function called
 * returns 0 on success or 1 if cipher is not recognized */
int crypt_set_encryption(const EVP_CIPHER* encryption, struct crypt_keys* fk){
	return_ifnull(fk, -1);

	if (fk->flag_encryption_set != 0){
		log_error("crypt_set_encryption() must be called after crypt_new()");
		return -1;
	}

	if (encryption){
		fk->encryption = encryption;
	}
	else{
		fk->encryption = EVP_enc_null();
	}

	fk->flag_encryption_set = 1;
	return 0;
}

/* generates a key and iv based on data
 * returns 0 on success or err on error */
int crypt_gen_keys(const void* data, int data_len, const EVP_MD* md, int iterations, struct crypt_keys* fk){
	return_ifnull(data, -1);
	return_ifnull(fk, -1);

	/* if md is NULL, default to sha256 */
	if (!md){
		md = EVP_sha256();
	}

	/* checking if encryption is set */
	if (fk->flag_encryption_set == 0){
		log_error("Encryption type was not set (call crypt_set_encryption())");
		return -1;
	}

	/* determining key and iv length */
	fk->key_length = EVP_CIPHER_key_length(fk->encryption);
	fk->iv_length = EVP_CIPHER_iv_length(fk->encryption);

	/* allocating space for key and iv */
	fk->key = malloc(fk->key_length);
	fk->iv = malloc(fk->iv_length);
	if (!fk->key || !fk->iv){
		log_enomem();
		return -1;
	}

	/* generating the key and iv */
	if (!EVP_BytesToKey(fk->encryption, md, fk->salt, data, data_len, iterations, fk->key, fk->iv)){
		log_error("Failed to generate keys from data");
		ERR_print_errors_fp(stderr);
		return -1;
	}

	fk->flag_keys_set = 1;
	return 0;
}

void crypt_free(struct crypt_keys* fk){
	if (!fk){
		return;
	}
	/* scrub keys so they can't be retrieved from memory */
	if (fk->flag_keys_set){
		crypt_scrub(fk->key, fk->key_length);
		crypt_scrub(fk->iv, fk->iv_length);
		free(fk->key);
		free(fk->iv);
	}
	free(fk);
}

void crypt_reset(struct crypt_keys* fk){
	if (!fk){
		return;
	}
	/* scrub keys so they can't be retrieved from memory */
	if (fk->flag_keys_set){
		crypt_scrub(fk->key, fk->key_length);
		crypt_scrub(fk->iv, fk->iv_length);
		free(fk->key);
		free(fk->iv);
	}
	memset(fk, 0, sizeof(*fk));
}

/* encrypts the file
 * returns 0 on success or err on error */
int crypt_encrypt_ex(const char* in, struct crypt_keys* fk, const char* out, int verbose, const char* progress_msg){
	/* do not want null terminator */
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_'};
	EVP_CIPHER_CTX* ctx = NULL;
	unsigned char inbuffer[BUFFER_LEN];
	unsigned char* outbuffer = NULL;
	FILE* fp_in = NULL;
	FILE* fp_out = NULL;
	int inlen;
	int outlen;
	int ret = 0;
	struct progress* p = NULL;

	/* checking null arguments */
	return_ifnull(in, -1);
	return_ifnull(fk, -1);
	return_ifnull(out, -1);

	fp_in = fopen(in, "rb");
	if (!fp_in){
		log_efopen(in);
		ret = -1;
		goto cleanup;
	}

	fp_out = fopen(out, "wb");
	if (!fp_out){
		log_efopen(out);
		ret = -1;
		goto cleanup;
	}

	/* checking if keys were actually generated */
	if (fk->flag_keys_set == 0){
		log_error("Encryption keys were not generated (call crypt_gen_keys())");
		ret = -1;
		goto cleanup;
	}

	/* write the salt prefix + salt to the file */
	fwrite(salt_prefix, 1, sizeof(salt_prefix), fp_out);
	if (ferror(fp_out)){
		log_efwrite(out);
		ret = -1;
		goto cleanup;
	}
	fwrite(fk->salt, 1, 8, fp_out);

	/* preparing progress bar */
	if (verbose){
		struct stat st;
		int fd;

		if (!progress_msg){
			progress_msg = "Encrypting file...";
		}

		fd = fileno(fp_in);
		fstat(fd, &st);

		p = start_progress(progress_msg, st.st_size);
	}

	/* allocating space for outbuffer */

	/* encrypted data is usually longer than input data,
	 * so we must use malloc instead of just making another
	 * buffer[BUFFER_LEN] */
	outbuffer = malloc(BUFFER_LEN + EVP_CIPHER_block_size(fk->encryption));
	if (!outbuffer){
		log_enomem();
		ret = -1;
		goto cleanup;
	}

	/* initializing encryption thingy */
	ctx = EVP_CIPHER_CTX_new();
	if (!ctx){
		log_error("Failed to initialize EVP_CIPHER_CTX");
		ERR_print_errors_fp(stderr);
		goto cleanup;
	}
	if (EVP_EncryptInit_ex(ctx, fk->encryption, NULL, fk->key, fk->iv) != 1){
		log_error("Failed to initialize encryption");
		ERR_print_errors_fp(stderr);
		ret = -1;
		goto cleanup;
	}

	/* while there is still data to be read in the file */
	while ((inlen = read_file(fp_in, inbuffer, sizeof(inbuffer))) > 0){
		/* encrypt it */
		if (EVP_EncryptUpdate(ctx, outbuffer, &outlen, inbuffer, inlen) != 1){
			log_error("Failed to encrypt data completely");
			ERR_print_errors_fp(stderr);
			ret = -1;
			goto cleanup;
		}
		/* write it to the out file */
		fwrite(outbuffer, 1, outlen, fp_out);
		if (ferror(fp_out)){
			log_efwrite(out);
			ret = -1;
			goto cleanup;
		}
		if (verbose){
			inc_progress(p, inlen);
		}
	}
	finish_progress(p);

	/* write any padding data to the file */
	if (EVP_EncryptFinal_ex(ctx, outbuffer, &outlen) != 1){
		log_error("Failed to write padding data to file");
		ERR_print_errors_fp(stderr);
		ret = -1;
		goto cleanup;
	}
	fwrite(outbuffer, 1, outlen, fp_out);

cleanup:
	if (fp_in && fclose(fp_in) != 0){
		log_efclose(in);
	}
	if (fp_out && fclose(fp_out) != 0){
		log_efclose(out);
	}
	if (ret != 0){
		remove(out);
	}
	if (ctx){
		EVP_CIPHER_CTX_free(ctx);
	}
	free(outbuffer);
	return ret;
}

/* simpler wrapper for crypt_encrypt_ex if progressbar is not needed */
int crypt_encrypt(const char* in, struct crypt_keys* fk, const char* fp_out){
	return crypt_encrypt_ex(in, fk, fp_out, 0, NULL);
}

/* extracts salt from encrypted file */
int crypt_extract_salt(const char* in, struct crypt_keys* fk){
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_' };
	char salt_buffer[8];
	char buffer[8];
	FILE* fp_in = NULL;
	unsigned i;

	/* checking null arguments */
	return_ifnull(in, -1);
	return_ifnull(fk, -1);

	fp_in = fopen(in, "rb");
	if (!fp_in){
		log_efopen(in);
		return -1;
	}

	/* check that fread works properly. also advances the file
	 * pointer to the beginning of the salt */
	if (fread(salt_buffer, 1, sizeof(salt_prefix), fp_in) != sizeof(salt_prefix)){
		log_error("Failed to read salt prefix from file");
		fclose(fp_in);
		return -1;
	}

	/* check that the prefix we read matches the salt prefix */
	if (memcmp(salt_buffer, salt_prefix, sizeof(salt_prefix)) != 0){
		log_error("File is not of the correct format");
		fclose(fp_in);
		return -1;
	}

	/* read the salt into the buffer, check if the correct
	 * amount of bytes were read */
	if (fread(buffer, 1, sizeof(buffer), fp_in) != sizeof(buffer)){
		log_error("Failed to read salt from file");
		fclose(fp_in);
		return -1;
	}

	/* memcpy() leaves data in memory
	 * bad for crypto applications */
	for (i = 0; i < sizeof(fk->salt); ++i){
		fk->salt[i] = buffer[i];
	}

	fk->flag_salt_extracted = 1;
	fclose(fp_in);
	return 0;
}

/* decrypts the file
 * returns 0 on success or err on error */
int crypt_decrypt_ex(const char* in, struct crypt_keys* fk, const char* out, int verbose, const char* progress_msg){
	/* do not want null terminator */
	EVP_CIPHER_CTX* ctx = NULL;
	unsigned char inbuffer[BUFFER_LEN];
	unsigned char* outbuffer = NULL;
	FILE* fp_in = NULL;
	FILE* fp_out = NULL;
	int inlen;
	int outlen;
	int ret = 0;
	struct progress* p = NULL;

	/* checking null arguments */
	return_ifnull(in, -1);
	return_ifnull(fk, -1);
	return_ifnull(out, -1);

	fp_in = fopen(in, "rb");
	if (!fp_in){
		log_efopen(in);
		ret = -1;
		goto cleanup;
	}

	fp_out = fopen(out, "wb");
	if (!fp_out){
		log_efopen(out);
		ret = -1;
		goto cleanup;
	}

	/* checking if keys were actually generated */
	if (fk->flag_keys_set == 0){
		log_error("Decryption keys were not generated (call crypt_gen_keys())");
		ret = -1;
		goto cleanup;
	}

	/* checking if salt was extracted */
	if (fk->flag_salt_extracted == 0){
		log_error("Salt was not extracted from the file (call crypt_extract_salt())");
		ret = -1;
		goto cleanup;
	}

	/* isn't the decrypted length supposed to be lower than
	 * the encrypted length?
	 *
	 * apparently not. that's why we have to do this malloc.
	 */
	outbuffer = malloc(BUFFER_LEN + EVP_CIPHER_block_size(fk->encryption));
	if (!outbuffer){
		log_enomem();
		ret = -1;
		goto cleanup;
	}

	/* preparing progress bar */
	if (verbose){
		struct stat st;
		int fd;

		if (!progress_msg){
			progress_msg = "Decrypting file...";
		}

		fd = fileno(fp_in);
		fstat(fd, &st);

		p = start_progress(progress_msg, st.st_size);
	}

	/* no need for malloc() like above, since the decrypted
	 * data is shorter than encrypted. */

	/* initializing cipher context */
	ctx = EVP_CIPHER_CTX_new();
	if (!ctx){
		log_error("Failed to initialize EVP_CIPHER_CTX");
		ERR_print_errors_fp(stderr);
		goto cleanup;
	}
	if (EVP_DecryptInit_ex(ctx, fk->encryption, NULL, fk->key, fk->iv) != 1){
		log_error("Failed to intitialize decryption process");
		ERR_print_errors_fp(stderr);
		ret = -1;
		goto cleanup;
	}

	/* advance file pointer beyond salt */
	fseek(fp_in, 8 + sizeof(fk->salt), SEEK_SET);

	/* while there is data in the input file */
	while ((inlen = read_file(fp_in, inbuffer, sizeof(inbuffer))) > 0){
		/* decrypt it */
		if (EVP_DecryptUpdate(ctx, outbuffer, &outlen, inbuffer, inlen) != 1){
			log_error("Failed to completely decrypt file");
			ERR_print_errors_fp(stderr);
			ret = -1;
			goto cleanup;
		}
		/* write it to the output file */
		fwrite(outbuffer, 1, outlen, fp_out);
		if (verbose){
			inc_progress(p, inlen);
		}
	}
	finish_progress(p);

	/* not sure what needs to be finalized, but ehh */
	if (EVP_DecryptFinal_ex(ctx, outbuffer, &outlen) != 1){
		log_error("Failed to write padding data to file");
		ERR_print_errors_fp(stderr);
		ret = -1;
		goto cleanup;
	}
	fwrite(outbuffer, 1, outlen, fp_out);

cleanup:
	if (fp_in && fclose(fp_in) != 0){
		log_efclose(in);
	}
	if (fp_out && fclose(fp_out) != 0){
		log_efclose(out);
	}
	if (ret != 0){
		remove(out);
	}
	free(outbuffer);
	if (ctx){
		EVP_CIPHER_CTX_free(ctx);
	}
	return ret;
}

int crypt_decrypt(const char* in, struct crypt_keys* fk, const char* fp_out){
	return crypt_decrypt_ex(in, fk, fp_out, 0, NULL);
}
