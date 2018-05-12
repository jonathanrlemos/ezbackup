/* options.h -- command-line/menu-based options parser
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __OPTIONS_H
#define __OPTIONS_H

/* compressor */
#include "maketar.h"
#include "cloud/include.h"
#include "stringarray.h"
#include <openssl/evp.h>

enum OPERATION{
	OP_INVALID = 0,
	OP_BACKUP  = 1,
	OP_RESTORE = 2,
	OP_CONFIGURE = 3,
	OP_EXIT = 4
};

struct options{
	char*                 prev_backup;
	struct string_array*  directories;
	struct string_array*  exclude;
	const EVP_MD*         hash_algorithm;
	const EVP_CIPHER*     enc_algorithm;
	char*                 enc_password;
	enum COMPRESSOR       comp_algorithm;
	int                   comp_level;
	char*                 output_directory;
	struct cloud_options* cloud_options;
	union tagflags{
		struct tagbits{
			unsigned      flag_verbose: 1;
		}bits;
		unsigned          dword;
	}flags;
};

void version(void);
void usage(const char* progname);
int display_menu(const char** options, int num_options, const char* title);
int parse_options_cmdline(int argc, char** argv, struct options** out, enum OPERATION* op_out);
int parse_options_menu(struct options* opt);
void free_options(struct options* o);
struct options* get_default_options(void);
int parse_options_fromfile(const char* file, struct options** output);
int write_options_tofile(const char* file, const struct options* opt);
int set_home_conf_dir(const char* dir);
int get_home_conf_dir(char** out);

#ifdef __UNIT_TESTING__
int get_home_conf_file(char** out);
#endif

#endif
