/* base.h
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CLOUD_BASE_H
#define __CLOUD_BASE_H

#include "cloud_options.h"
#include <sys/stat.h>

struct cloud_data;

/**
 * @brief Logs in to a cloud account.
 *
 * @param co Cloud options containing a username, password, and cloud provider to log into.
 *
 * @param out_cd A handle containing the cloud account's information.
 * This will be set to NULL if the login fails.
 * This structure must be freed with cloud_logout() when no longer in use.
 * @see cloud_logout
 *
 * @return 0 on success, or negative on failure.
 */
int cloud_login(struct cloud_options* co, struct cloud_data** out_cd);

/**
 * @brief Makes a directory within a cloud account.
 *
 * @param dir Name of the directory to create.
 *
 * @param cd A cloud data structure returned by cloud_login()
 * @see cloud_login().
 *
 * @return 0 on success, or negative on failure.
 */
int cloud_mkdir(const char* dir, struct cloud_data* cd);

/**
 * @brief Lets the user choose a directory to create within a cloud account.
 *
 * The prompt will look like the following:
 * /base/dir/[user input]
 *
 * If the user's input creates more than one subdirectory (/base/dir/newdir1/newdir2), each subdirectory will be recursively created.
 * The user can choose not to create a directory by entering a blank string after the base directory, in which case a directory is not created, and a positive value is returned to reflect this.
 *
 * @param base_dir The directory to create one or more subdirectories under.
 *
 * @param chosen_dir A pointer to a string that will contain the full directory that the user entered.
 * This argument can be NULL, in which case it is not used.
 * Otherwise, this string must be free()'d when no longer in use.
 * If the user does not create a directory or there was an error, the location that this points to will be set to NULL.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, positive if the user chose not to create a directory, negative on failure.
 */
int cloud_mkdir_ui(const char* base_dir, char** chosen_dir, struct cloud_data* cd);

/**
 * @brief Fills a stat structure for a file in a cloud account.
 * At the moment, the uid/gid values are always set to the current user's values, and permissions are always set to 0755.
 * TODO: implement support for these values.
 *
 * @param dir_or_file The directory or file to stat.
 *
 * @param out A pointer to a stat structure to be filled.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, negative on failure.
 */
int cloud_stat(const char* dir_or_file, struct stat* out, struct cloud_data* cd);

/**
 * @brief Uploads a file to a cloud account.
 *
 * @param in_file Path to a file on disk to upload to the cloud.
 *
 * @param upload_dir A directory to upload the file to. This can also include the filename.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, negative on failure.
 */
int cloud_upload(const char* in_file, const char* upload_dir, struct cloud_data* cd);

/**
 * @brief Lets the user choose a directory to upload to.
 *
 * This pulls up a cli file explorer that allows the user to create/select a directory.
 * The user can choose to exit, in which case a file is not uploaded, and a positive value is returned to reflect this.
 *
 * Unlike cloud_upload(), this function does not let the user specify the uploaded filename. That will always be equal to the filename on disk.
 *
 * @param in_file Path to a file on disk to upload to the cloud.
 *
 * @param base_dir The directory to start the file explorer within.
 *
 * @param chosen_path A pointer to a string that will contain the full directory that the user chose.
 * This argument can be NULL, in which case it is not used.
 * Otherwise, this string must be free()'d when no longer in use.
 * If the user does not choose a directory or there was an error, the location that this points to will be set to NULL.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, positive if a directory was not chosen, negative on failure.
 */
int cloud_upload_ui(const char* in_file, const char* base_dir, char** chosen_path, struct cloud_data* cd);

/**
 * @brief Downloads a file from a cloud account.
 *
 * @param download_path Path to a file within the cloud to download to disk.
 *
 * @param out_file A pointer to a string that contains the output file.
 * This parameter cannot be NULL, but can point to NULL.
 * If it points to NULL, the string it points to will be filled with the following format:
 * [Current Directory]/[filename]
 * If this happens, the output string must be free()'d when no longer in use, even if this function fails.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, negative on failure.
 */
int cloud_download(const char* download_path, char** out_file, struct cloud_data* cd);

/**
 * @brief Lets the user choose a file to download.
 *
 * This pulls up a cli file explorer that allows the user to choose a file.
 * The user can choose to exit, in which case a file is not downloaded, and a positive value is returned to reflect this.
 *
 * @param base_dir The directory to start the file explorer within.
 *
 * @param out_file A pointer to a string that contains the output file.
 * This parameter cannot be NULL, but can point to NULL.
 * If it points to NULL, the string it points to will be filled with the following format:
 * [Current Directory]/[filename]
 * If this happens, the output string must be free()'d when no longer in use, even if this function fails.
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, positive if a file was not chosen, negative on failure.
 */
int cloud_download_ui(const char* base_dir, char** out_file, struct cloud_data* cd);

/**
 * @brief Removes a file or directory from a cloud account.
 *
 * @param dir_or_file A file or directory to remove.
 * A directory can only be removed if it is empty.
 *
 * @param cd A cloud data structure returned by cloud_login()
 * @see cloud_login()
 *
 * @return 0 on success, negative on failure.
 */
int cloud_remove(const char* dir_or_file, struct cloud_data* cd);

/**
 * @brief Lets the user choose a file to remove.
 *
 * This pulls up a cli file explorer that allows the user to choose a file.
 * The user can choose to exit, in which case a file is not removed, and a positive value is returned to reflect this.
 *
 * At the moment this does not allow the user to remove a directory.
 * TODO: make this happen.
 *
 * @param base_dir The directory to start the file explorer within.
 *
 * @param chosen_path A pointer to a string that will contain the path to the file that the user chose.
 * This argument can be NULL, in which case it is not used.
 * Otherwise, this string must be free()'d when no longer in use.
 * If the user does not choose a file or there was an error, the location that this points to will be set to NULL.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * @see cloud_login()
 *
 * @return 0 on success, positive if a file was not chosen, negative on failure.
 */
int cloud_remove_ui(const char* base_dir, char** chosen_file, struct cloud_data* cd);

/**
 * @brief Logs out of a cloud account and frees all memory associated with the structure.
 *
 * @param cd A cloud data structure returned by cloud_login().
 * This argument can be NULL, in which case this function does nothing.
 * @see cloud_login
 *
 * @return 0 on success, negative on failure.
 */
int cloud_logout(struct cloud_data* cd);

#endif