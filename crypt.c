/* prototypes */
#include "crypt.h"
/* read_file() */
#include "readfile.h"
/* handling errors */
#include "evperror.h"
/* progress bar */
#include "progressbar.h"
/* file size */
#include <sys/stat.h>
/* the special sauce */
#include <openssl/evp.h>
/* random numbers to generate salt and gen_csrand memory */
#include <openssl/rand.h>
/* malloc() */
#include <stdlib.h>
/* read from /dev/urandom if not enough entropy */
#include <stdio.h>
#include <unistd.h>
/* memcmp */
#include <string.h>

#define FLAG_UNINITIALIZED (0x0)
#define FLAG_ENCRYPTION_SET (0x1)
#define FLAG_KEYS_SET (0x2)
#define FLAG_SALT_EXTRACTED (0x4)

/* generates cryptographically-secure random data and
 * writes it to data, or writes low-grade random data on error
 *
 * returns 0 on success, err on error. */
int crypt_scrub(unsigned char* data, int len){
	int res;
	unsigned long err;

	/* checking if data is not NULL */
	if (!data){
		return ERR_ARGUMENT_NULL;
	}

	res = RAND_bytes(data, len);
	err = evp_geterror();

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
			fprintf(stderr, "Not enough entropy to generate cryptographically-secure random numbers, and could not open /dev/urandom for reading. Aborting...");
			exit(1);
		}

		for (i = 0; i < len; ++i){
			data_vol[i] = fgetc(fp);
		}
		fclose(fp);
		return err;
	}
	return 0;
}
/* crypt_scrub() is really just a csrand generator */
#define gen_csrand(data, len) ((crypt_scrub(data, len)))

unsigned char crypt_randc(void){
	unsigned char ret;
	int res;

	res = RAND_bytes(&ret, 1);
	if (res != 1){
		FILE* fp = fopen("/dev/urandom", "rb");
		if (!fp){
			return random() % 256;
		}
		ret = fgetc(fp);
		fclose(fp);
		return ret;
	}
	return ret;
}

/* generates a random salt
 * returns 0 if salt is csrand, err if not */
int crypt_gen_salt(crypt_keys* fk){
	if (!fk){
		return ERR_ARGUMENT_NULL;
	}
	return gen_csrand(fk->salt, sizeof(fk->salt));
}

/* sets a user-defined salt
 * always returns 0 */
int crypt_set_salt(unsigned char salt[8], crypt_keys* fk){
	unsigned i;

	if (!fk){
		return ERR_ARGUMENT_NULL;
	}

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

/* sets encryption type, this must be the first function called
 * returns 0 on success or 1 if cipher is not recognized */
int crypt_set_encryption(const char* encryption, crypt_keys* fk){
	if (!fk){
		return ERR_ARGUMENT_NULL;
	}

	fk->flags = FLAG_UNINITIALIZED;

	if (encryption){
		/* allows EVP_get_cipherbyname to work */
		OpenSSL_add_all_algorithms();
		fk->encryption = EVP_get_cipherbyname(encryption);
		if (!fk->encryption){
			return evp_geterror();
		}
	}
	/* if encryption is NULL, use no encryption */
	else{
		fk->encryption = EVP_enc_null();
	}

	fk->flags |= FLAG_ENCRYPTION_SET;
	return 0;
}

/* generates a key and iv based on data
 * returns 0 on success or err on error */
int crypt_gen_keys(
		unsigned char* data,
		int data_len,
		const EVP_MD* md,
		int iterations,
		crypt_keys* fk
		){

	if (!fk || !data){
		return ERR_ARGUMENT_NULL;
	}

	/* if md is NULL, default to sha256 */
	if (!md){
		md = EVP_sha256();
	}

	/* checking if encryption and salt are set */
	if (fk->flags != FLAG_ENCRYPTION_SET){
		return ERR_ENCRYPTION_UNINITIALIZED;
	}

	/* determining key and iv length */
	fk->key_length = EVP_CIPHER_key_length(fk->encryption);
	fk->iv_length = EVP_CIPHER_iv_length(fk->encryption);

	/* allocating space for key and iv */
	fk->key = malloc(fk->key_length);
	fk->iv = malloc(fk->iv_length);
	if (!fk->key || !fk->iv){
		return ERR_OUT_OF_MEMORY;
	}

	/* generating the key and iv */
	if (!EVP_BytesToKey(fk->encryption, md, fk->salt, data, data_len, iterations, fk->key, fk->iv)){
		return evp_geterror();
	}

	fk->flags |= FLAG_KEYS_SET;
	return 0;
}

/* frees memory malloc'd by crypt_gen_keys()
 * returns 0 if fk is not NULL */
int crypt_free(crypt_keys* fk){
	if (!fk){
		return ERR_ARGUMENT_NULL;
	}

	/* scrub keys so they can't be retrieved from memory */
	crypt_scrub(fk->key, fk->key_length);
	crypt_scrub(fk->iv, fk->iv_length);

	free(fk->key);
	free(fk->iv);

	return 0;
}

/* encrypts the file
 * returns 0 on success or err on error */
int crypt_encrypt(const char* in, crypt_keys* fk, const char* out){
	/* do not want null terminator */
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_'};
	EVP_CIPHER_CTX* ctx = NULL;
	unsigned char inbuffer[BUFFER_LEN];
	unsigned char* outbuffer = NULL;
	int inlen;
	int outlen;
	int ret = 0;
	FILE* fp_in;
	FILE* fp_out;

	if (!in || !fk || !out){
		return ERR_ARGUMENT_NULL;
	}

	/* checking if keys were actually generated */
	if (!(fk->flags & FLAG_KEYS_SET)){
		return ERR_KEYS_UNINITIALIZED;
	}

	fp_in = fopen(in, "rb");
	if (!fp_in){
		return ERR_FILE_INPUT;
	}
	fp_out = fopen(out, "wb");
	if (!fp_out){
		fclose(fp_in);
		return ERR_FILE_OUTPUT;
	}

	/* dealing with salt */
	fwrite(salt_prefix, 1, sizeof(salt_prefix), fp_out);
	if (ferror(fp_out)){
		ret = ERR_FILE_OUTPUT;
		goto cleanup;
	}
	fwrite(fk->salt, 1, 8, fp_out);

	/* initializing cipher context and preparing outbuffer */
	/* encrypted data is usually longer than input data */
	outbuffer = malloc(BUFFER_LEN + EVP_CIPHER_block_size(fk->encryption));
	if (!outbuffer){
		ret = ERR_OUT_OF_MEMORY;
		goto cleanup;
	}

	ctx = EVP_CIPHER_CTX_new();
	ret = evp_geterror();
	if (ret){
		goto cleanup;
	}
	if (EVP_EncryptInit_ex(ctx, fk->encryption, NULL, fk->key, fk->iv) != 1){
		ret = evp_geterror();
		goto cleanup;
	}

	/* reading + encrypting file */
	while ((inlen = read_file(fp_in, inbuffer, sizeof(inbuffer)))){
		if (EVP_EncryptUpdate(ctx, outbuffer, &outlen, inbuffer, inlen) != 1){
			ret = evp_geterror();
			goto cleanup;
		}
		fwrite(outbuffer, 1, outlen, fp_out);
	}

	/* finalizing */
	if (EVP_EncryptFinal_ex(ctx, outbuffer, &outlen) != 1){
		ret = evp_geterror();
		goto cleanup;
	}
	fwrite(outbuffer, 1, outlen, fp_out);

	/* cleanup */
cleanup:
	fclose(fp_in);
	fclose(fp_out);
	if (ctx){
		EVP_CIPHER_CTX_free(ctx);
	}
	free(outbuffer);
	return 0;

}

/* encrypts the file
 * returns 0 on success or err on error */
int crypt_encrypt_ex(const char* in, crypt_keys* fk, const char* out, int verbose, const char* progress_msg){
	/* do not want null terminator */
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_'};
	EVP_CIPHER_CTX* ctx = NULL;
	unsigned char inbuffer[BUFFER_LEN];
	unsigned char* outbuffer = NULL;
	int inlen;
	int outlen;
	int ret = 0;
	progress* p;
	FILE* fp_in;
	FILE* fp_out;

	if (!in || !fk || !out){
		return ERR_ARGUMENT_NULL;
	}

	/* checking if keys were actually generated */
	if (!(fk->flags & FLAG_KEYS_SET)){
		return ERR_KEYS_UNINITIALIZED;
	}

	fp_in = fopen(in, "rb");
	if (!fp_in){
		return ERR_FILE_INPUT;
	}
	fp_out = fopen(out, "wb");
	if (!fp_out){
		fclose(fp_in);
		return ERR_FILE_OUTPUT;
	}

	/* dealing with salt */
	fwrite(salt_prefix, 1, sizeof(salt_prefix), fp_out);
	if (ferror(fp_out)){
		ret = ERR_FILE_OUTPUT;
		goto cleanup;
	}
	fwrite(fk->salt, 1, 8, fp_out);

	/* preparing progress bar */
	if (verbose){
		struct stat st;
		int fd;

		fd = fileno(fp_in);
		fstat(fd, &st);

		p = start_progress(progress_msg, st.st_size);
	}

	/* initializing cipher context and preparing outbuffer */
	/* encrypted data is usually longer than input data */
	outbuffer = malloc(BUFFER_LEN + EVP_CIPHER_block_size(fk->encryption));
	if (!outbuffer){
		ret = ERR_OUT_OF_MEMORY;
		goto cleanup;
	}

	ctx = EVP_CIPHER_CTX_new();
	ret = evp_geterror();
	if (ret){
		goto cleanup;
	}
	if (EVP_EncryptInit_ex(ctx, fk->encryption, NULL, fk->key, fk->iv) != 1){
		ret = evp_geterror();
		goto cleanup;
	}

	/* reading + encrypting file */
	while ((inlen = read_file(fp_in, inbuffer, sizeof(inbuffer)))){
		if (EVP_EncryptUpdate(ctx, outbuffer, &outlen, inbuffer, inlen) != 1){
			ret = evp_geterror();
			goto cleanup;
		}
		fwrite(outbuffer, 1, outlen, fp_out);
		if (verbose){
			inc_progress(p, outlen);
		}
	}
	finish_progress(p);

	/* finalizing */
	if (EVP_EncryptFinal_ex(ctx, outbuffer, &outlen) != 1){
		ret = evp_geterror();
		goto cleanup;
	}
	fwrite(outbuffer, 1, outlen, fp_out);

	/* cleanup */
cleanup:
	fclose(fp_in);
	fclose(fp_out);
	if (ctx){
		EVP_CIPHER_CTX_free(ctx);
	}
	free(outbuffer);
	return 0;
}

int crypt_extract_salt(const char* in, crypt_keys* fk){
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_' };
	char buffer[8];
	unsigned i;
	FILE* fp;

	if (!in || !fk){
		return ERR_ARGUMENT_NULL;
	}

	fp = fopen(in, "rb");
	if (!fp){
		return ERR_FILE_INPUT;
	}

	if (fread(buffer, 1, sizeof(salt_prefix), fp) != sizeof(salt_prefix)){
		fclose(fp);
		return ERR_FILE_INPUT;
	}

	if (memcmp(salt_prefix, buffer, sizeof(salt_prefix))){
		fclose(fp);
		return ERR_FILE_INVALID;
	}

	if (sizeof(fk->salt) != sizeof(buffer)){
		fclose(fp);
		return ERR_FUBAR;
	}

	if (fread(buffer, 1, sizeof(buffer), fp) != sizeof(buffer)){
		fclose(fp);
		return ERR_FILE_INPUT;
	}

	/* memcpy() leaves data in memory
	 * bad for crypto applications */
	for (i = 0; i < sizeof(fk->salt); ++i){
		fk->salt[i] = buffer[i];
	}

	fk->flags |= FLAG_SALT_EXTRACTED;
	fclose(fp);
	return 0;
}

/* encrypts the file
 * returns 0 on success or err on error */
int crypt_decrypt(const char* in, crypt_keys* fk, const char* out){
	/* do not want null terminator */
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_'};
	EVP_CIPHER_CTX* ctx = NULL;
	unsigned char inbuffer[BUFFER_LEN];
	unsigned char outbuffer[BUFFER_LEN];
	int inlen;
	int outlen;
	int ret = 0;
	FILE* fp_in;
	FILE* fp_out;

	if (!in || !fk || !out){
		return ERR_ARGUMENT_NULL;
	}

	/* checking if keys were actually generated */
	if (!(fk->flags & FLAG_KEYS_SET)){
		return ERR_KEYS_UNINITIALIZED;
	}

	fp_in = fopen(in, "rb");
	if (!fp_in){
		return ERR_FILE_INPUT;
	}
	fp_out = fopen(out, "wb");
	if (!fp_out){
		fclose(fp_in);
		return ERR_FILE_OUTPUT;
	}

	/* dealing with salt */
	fwrite(salt_prefix, 1, sizeof(salt_prefix), fp_out);
	if (ferror(fp_out)){
		ret = ERR_FILE_OUTPUT;
		goto cleanup;
	}
	fwrite(fk->salt, 1, 8, fp_out);

	/* initializing cipher context */
	ctx = EVP_CIPHER_CTX_new();
	ret = evp_geterror();
	if (ret){
		goto cleanup;
	}
	if (EVP_DecryptInit_ex(ctx, fk->encryption, NULL, fk->key, fk->iv) != 1){
		ret = evp_geterror();
		goto cleanup;
	}

	/* reading + encrypting file */
	while ((inlen = read_file(fp_in, inbuffer, sizeof(inbuffer)))){
		if (EVP_DecryptUpdate(ctx, outbuffer, &outlen, inbuffer, inlen) != 1){
			ret = evp_geterror();
			goto cleanup;
		}
		fwrite(outbuffer, 1, outlen, fp_out);
	}

	/* finalizing */
	if (EVP_DecryptFinal_ex(ctx, outbuffer, &outlen) != 1){
		ret = evp_geterror();
		goto cleanup;
	}
	fwrite(outbuffer, 1, outlen, fp_out);

	/* cleanup */
cleanup:
	fclose(fp_in);
	fclose(fp_out);
	if (ctx){
		EVP_CIPHER_CTX_free(ctx);
	}
	return 0;
}

/* decrypts the file
 * returns 0 on success or err on error */
int crypt_decrypt_ex(const char* in, crypt_keys* fk, const char* out, int verbose, const char* progress_msg){
	/* do not want null terminator */
	const char salt_prefix[8] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_'};
	EVP_CIPHER_CTX* ctx = NULL;
	unsigned char inbuffer[BUFFER_LEN];
	unsigned char outbuffer[BUFFER_LEN];
	int inlen;
	int outlen;
	int ret = 0;
	progress* p;
	FILE* fp_in;
	FILE* fp_out;

	if (!in || !fk || !out){
		return ERR_ARGUMENT_NULL;
	}

	/* checking if keys were actually generated */
	if (!(fk->flags & FLAG_KEYS_SET)){
		return ERR_KEYS_UNINITIALIZED;
	}

	fp_in = fopen(in, "rb");
	if (!fp_in){
		return ERR_FILE_INPUT;
	}
	fp_out = fopen(out, "wb");
	if (!fp_out){
		fclose(fp_in);
		return ERR_FILE_OUTPUT;
	}

	/* dealing with salt */
	fwrite(salt_prefix, 1, sizeof(salt_prefix), fp_out);
	if (ferror(fp_out)){
		ret = ERR_FILE_OUTPUT;
		goto cleanup;
	}
	fwrite(fk->salt, 1, 8, fp_out);

	/* preparing progress bar */
	if (verbose){
		struct stat st;
		int fd;

		fd = fileno(fp_in);
		fstat(fd, &st);

		p = start_progress(progress_msg, st.st_size);
	}

	/* initializing cipher context */
	/* encrypted data is usually longer than input data */
	ctx = EVP_CIPHER_CTX_new();
	ret = evp_geterror();
	if (ret){
		goto cleanup;
	}
	if (EVP_DecryptInit_ex(ctx, fk->encryption, NULL, fk->key, fk->iv) != 1){
		ret = evp_geterror();
		goto cleanup;
	}

	/* reading + encrypting file */
	while ((inlen = read_file(fp_in, inbuffer, sizeof(inbuffer)))){
		if (EVP_DecryptUpdate(ctx, outbuffer, &outlen, inbuffer, inlen) != 1){
			ret = evp_geterror();
			goto cleanup;
		}
		fwrite(outbuffer, 1, outlen, fp_out);
		if (verbose){
			inc_progress(p, outlen);
		}
	}
	finish_progress(p);

	/* finalizing */
	if (EVP_DecryptFinal_ex(ctx, outbuffer, &outlen) != 1){
		ret = evp_geterror();
		goto cleanup;
	}
	fwrite(outbuffer, 1, outlen, fp_out);

	/* cleanup */
cleanup:
	fclose(fp_in);
	fclose(fp_out);
	if (ctx){
		EVP_CIPHER_CTX_free(ctx);
	}
	return 0;
}
