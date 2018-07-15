/* checksumsort.h
 *
 * Copyright (c) 2018 Jonathan Lemos
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CHECKSUMSORT_H
#define __CHECKSUMSORT_H

#include <stdio.h>
#include "filehelper.h"

#ifndef MAX_RUN_SIZE
#define MAX_RUN_SIZE (1 << 24) /**< The maximum length of a checksum run (16MB). */
#endif

/**
 * @brief Holds the data needed for a checksum entry.
 */
struct element{
	char* file;     /**< The filename. */
	char* checksum; /**< The null-ternimated hexadecimal checksum string corresponding to the file's contents. */
};

/**
 * @brief Acts as a node in the merging heap.
 * The index of the file is necessary for merge_files() to draw the next element from the correct file.
 * @see merge_files()
 */
struct minheapnode{
	struct element* e; /**< The element. @see struct element */
	int i;             /**< The index of the file it came from */
};

/**
 * @brief Writes an element to a checksum file.
 *
 * Format: /path/to/file\0ABCDEF123456\n
 *
 * @param fp The output file.
 * This FILE* must be opened in writing binary ("wb") mode.
 *
 * @param e The element to write.
 * @return 0 on success, negative on failure.
 */
int write_element_to_file(FILE* fp, struct element* e);

/* TODO: return an integer since there's multiple reasons for NULL */
/**
 * @brief Retrieves an element from a checksum file.
 *
 * @param fp The input file.
 * This FILE* must be opened in reading binary ("rb") mode.
 *
 * @return A struct element* containing the next file/checksum, or NULL on end-of-file/error.
 * This struct element* must be free()'d when no longer in use.
 * @see free_element()
 */
struct element* get_next_checksum_element(FILE* fp);

/* TODO: return an integer since there's multiple reasons for NULL */
/**
 * @brief Retrieves the checksum element at a certain index.
 *
 * This function is slow because it rewinds the fp and moves sequentially to the n'th element.
 *
 * @param fp The input file.
 * This FILE* must be opened in reading binary("rb") mode.
 *
 * @param index The index of the element to return from the file.
 *
 * @return A struct element* containing the file/checksum at the index, or NULL on end-of-file/out-of-bounds/error.
 * This struct element* must be free()'d when no longer in use.
 * @see free_element()
 */
struct element* get_checksum_element_index(FILE* fp, int index);

/**
 * @brief Returns the index of the median element of {low, mid, high}
 *
 * This function is meant to provide a pivot for quicksort_elements() that divides the list somewhat evenly.
 * This allows quicksort_elements() to sort more quickly and reliably.
 * @see quicksort_elements()
 *
 * @param elements The elements to choose a pivot from.
 *
 * @param low The lowest element to choose from.
 *
 * @param high The highest element to choose from.
 *
 * @return The index of the median element of {low, mid, high}
 */
int median_of_three(struct element** elements, int low, int high);

/**
 * @brief Quicksorts a list of elements.
 *
 * This sorts the initial runs for create_initial_runs().
 * @see create_initial_runs()
 *
 * @param elements The elements to sort.
 *
 * @param size The length of the elements array.
 *
 * @return void
 */
void quicksort_elements(struct element** elements, int low, int high);

/**
 * @brief Frees all memory associated with an element.
 *
 * @param e The element to free.
 * This can be NULL, in which case this function does nothing.
 *
 * @return void
 */
void free_element(struct element* e);

/**
 * @brief Frees all memory associated with an array of elements.
 *
 * @param elements The elements to free.
 * If this is NULL, size must be 0, otherwise a segmentation fault will happen.
 *
 * @param size The length of the elements array.
 *
 * @return void
 */
void free_element_array(struct element** elements, size_t size);

/**
 * @brief Creates an array of individually sorted checksum lists from a single unsorted checksum list.
 *
 * This is necessary to allow us to sort a checksum file larger than the available RAM on the system.
 * Each of the files are sorted individually, but the files are not sorted relative to one another.
 * The maximum length of a file is controlled with MAX_RUN_LEN
 * @see MAX_RUN_LEN
 * @see merge_files()
 *
 * @param in_file The unsorted checksum file.
 * This FILE* must be opened in reading binary ("rb") mode.
 *
 * @param out A pointer to an output array of temporary files.
 * This will be set to NULL on error.
 * @see struct TMPFILE
 *
 * @param n_files The number of files in the output array.
 * This will be set to 0 on error.
 *
 * @return 0 on success, or negative on error.
 */
int create_initial_runs(FILE* in_file, struct TMPFILE*** out, size_t* n_files);

/**
 * @brief Merges the files created by create_initial_runs() into a single sorted checksum list.
 *
 * This function does not free the temporary files. That must be done by the caller.
 * @see create_initial_runs()
 *
 * @param in The individually sorted checksum files created by create_initial_runs()
 * Undefined behavior if the files are not individually sorted.
 *
 * @param n_files The number of files to merge.
 *
 * @param out_file The output file.
 * This FILE* must be opened in writing binary ("wb") mode.
 *
 * @return 0 on success, or negative on error.
 */
int merge_files(struct TMPFILE** in, size_t n_files, FILE* out_file);

/**
 * @brief Searches a sorted checksum list for a filename, and returns its checksum if it exists.
 *
 * @param fp A sorted checksum list.
 * This FILE* must be opened in reading binary ("rb") mode.
 * Undefined behavior if this list is not sorted.
 * @see sort_checksum_file()
 *
 * @param key The filename to search for.
 *
 * @param checksum A pointer to the output checksum location.
 * The output will be a null-terminated hexadecimal checksum string, or NULL if the key could not be found or there was an error.
 *
 * @return 0 on success, positive if the checksum could not be found, negative on error.
 */
int search_file(FILE* fp, const char* key, char** checksum);

#endif
