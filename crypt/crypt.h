/* crypt.h -- file encryption/decryption
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CRYPT_CRYPT_H
#define __CRYPT_CRYPT_H

/* list of encryption algorithms */
#include <openssl/evp.h>

/* holds data needed for encryption
 *
 * key and iv are automatically scrubbed from memory
 * once they are used. */
struct crypt_keys{
	unsigned char* key;
	int key_length;
	unsigned char* iv;
	int iv_length;
	unsigned char salt[8];
	const EVP_CIPHER* encryption;
	/* tells if functions were called in the right order
	 * otherwise we'll get cryptic hard-to-debug sigsegv's */
	unsigned flag_encryption_set: 1;
	unsigned flag_keys_set: 1;
	unsigned flag_salt_extracted: 1;
};

struct crypt_keys* crypt_new(void);
int crypt_scrub(void* data, int len);
/* crypt_scrub() is really just a csrand generator */
#define gen_csrand(data, len) ((crypt_scrub(data, len)))
unsigned char crypt_randc(void);
const EVP_CIPHER* crypt_get_cipher(const char* encryption_name);
int crypt_set_encryption(const EVP_CIPHER* encryption, struct crypt_keys* fk);
int crypt_gen_salt(struct crypt_keys* fk);
int crypt_set_salt(const unsigned char salt[8], struct crypt_keys* fk);
int crypt_gen_keys(const unsigned char* data, int data_len, const EVP_MD* md, int iterations, struct crypt_keys* fk);
int crypt_encrypt(const char* in, struct crypt_keys* fk, const char* out);
int crypt_encrypt_ex(const char* in, struct crypt_keys* fk, const char* out, int verbose, const char* progress_msg);
int crypt_decrypt(const char* in, struct crypt_keys* fk, const char* out);
int crypt_decrypt_ex(const char* in, struct crypt_keys* fk, const char* out, int verbose, const char* progress_msg);
int crypt_extract_salt(const char* in, struct crypt_keys* fk);
int crypt_free(struct crypt_keys* fk);

#endif