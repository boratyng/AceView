/* sa.gpusort.h
 * A function that sorts on a GPU callable from C
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

void saGPUSort (char *cp, long int number_of_records, int type);

void saGPUMatchHits(const char* index_path, unsigned int num_chunks);

#ifdef __cplusplus
}
#endif
