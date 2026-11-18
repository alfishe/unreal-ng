/*
 * One translation unit for rasm's single-file Exomizer 2 (exomizer.h, unmodified from rasm 3.3.x), plus the entry
 * points unreal-asm calls. The library keeps state in statics (search buffers, option tables), so the callers hold a
 * lock (compress.cpp). Every external symbol of the library is renamed with the unrealasm_exo_ prefix so that it never
 * clashes with another library linked into the same program (it uses names such as get, membuf_init, read_file).
 */
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exomizerunit.h"

/* rasm's allocator, used once by Exomizer_crunch */
#define MemMalloc malloc

/* Generated from nm of this unit without the renames */
#define Exomizer_crunch unrealasm_exo_Exomizer_crunch
#define G_log_ctx unrealasm_exo_G_log_ctx
#define G_log_level unrealasm_exo_G_log_level
#define G_log_log_level unrealasm_exo_G_log_log_level
#define bits_dump unrealasm_exo_bits_dump
#define chunkpool_calloc unrealasm_exo_chunkpool_calloc
#define chunkpool_free unrealasm_exo_chunkpool_free
#define chunkpool_free2 unrealasm_exo_chunkpool_free2
#define chunkpool_init unrealasm_exo_chunkpool_init
#define chunkpool_malloc unrealasm_exo_chunkpool_malloc
#define crunch_backwards unrealasm_exo_crunch_backwards
#define dec_ctx_decrunch unrealasm_exo_dec_ctx_decrunch
#define dec_ctx_free unrealasm_exo_dec_ctx_free
#define dec_ctx_init unrealasm_exo_dec_ctx_init
#define decrunch_backwards unrealasm_exo_decrunch_backwards
#define do_compress unrealasm_exo_do_compress
#define do_output unrealasm_exo_do_output
#define dump_matches unrealasm_exo_dump_matches
#define exocrunch unrealasm_exo_exocrunch
#define exodecrunch unrealasm_exo_exodecrunch
#define find_sys unrealasm_exo_find_sys
#define fixup_appl unrealasm_exo_fixup_appl
#define flagarg unrealasm_exo_flagarg
#define flagflag unrealasm_exo_flagflag
#define flagind unrealasm_exo_flagind
#define freq_stats_dump unrealasm_exo_freq_stats_dump
#define freq_stats_dump_raw unrealasm_exo_freq_stats_dump_raw
#define get unrealasm_exo_get
#define get_bits unrealasm_exo_get_bits
#define get_byte unrealasm_exo_get_byte
#define get_cooked_code_phase2 unrealasm_exo_get_cooked_code_phase2
#define get_gamma_code unrealasm_exo_get_gamma_code
#define getflag unrealasm_exo_getflag
#define handle_base_flags unrealasm_exo_handle_base_flags
#define handle_crunch_flags unrealasm_exo_handle_crunch_flags
#define hex_dump unrealasm_exo_hex_dump
#define load_located unrealasm_exo_load_located
#define log_add_output_stream unrealasm_exo_log_add_output_stream
#define log_delete unrealasm_exo_log_delete
#define log_log unrealasm_exo_log_log
#define log_log_default unrealasm_exo_log_log_default
#define log_new unrealasm_exo_log_new
#define log_set_level unrealasm_exo_log_set_level
#define log_vlog unrealasm_exo_log_vlog
#define match_ctx_free unrealasm_exo_match_ctx_free
#define match_ctx_init unrealasm_exo_match_ctx_init
#define match_new unrealasm_exo_match_new
#define matches_get unrealasm_exo_matches_get
#define matchp_cache_enum_get_next unrealasm_exo_matchp_cache_enum_get_next
#define matchp_cache_get_enum unrealasm_exo_matchp_cache_get_enum
#define matchp_snp_enum_get_next unrealasm_exo_matchp_snp_enum_get_next
#define matchp_snp_get_enum unrealasm_exo_matchp_snp_get_enum
#define membuf_append unrealasm_exo_membuf_append
#define membuf_append_char unrealasm_exo_membuf_append_char
#define membuf_atleast unrealasm_exo_membuf_atleast
#define membuf_atmost unrealasm_exo_membuf_atmost
#define membuf_clear unrealasm_exo_membuf_clear
#define membuf_delete unrealasm_exo_membuf_delete
#define membuf_free unrealasm_exo_membuf_free
#define membuf_get unrealasm_exo_membuf_get
#define membuf_get_size unrealasm_exo_membuf_get_size
#define membuf_init unrealasm_exo_membuf_init
#define membuf_insert unrealasm_exo_membuf_insert
#define membuf_memcpy unrealasm_exo_membuf_memcpy
#define membuf_memlen unrealasm_exo_membuf_memlen
#define membuf_new unrealasm_exo_membuf_new
#define membuf_remove unrealasm_exo_membuf_remove
#define membuf_trim unrealasm_exo_membuf_trim
#define membuf_truncate unrealasm_exo_membuf_truncate
#define optimal_dump unrealasm_exo_optimal_dump
#define optimal_encode unrealasm_exo_optimal_encode
#define optimal_encode_int unrealasm_exo_optimal_encode_int
#define optimal_encoding_export unrealasm_exo_optimal_encoding_export
#define optimal_encoding_import unrealasm_exo_optimal_encoding_import
#define optimal_free unrealasm_exo_optimal_free
#define optimal_init unrealasm_exo_optimal_init
#define optimal_optimize unrealasm_exo_optimal_optimize
#define optimal_out unrealasm_exo_optimal_out
#define output_bits unrealasm_exo_output_bits
#define output_bits_flush unrealasm_exo_output_bits_flush
#define output_byte unrealasm_exo_output_byte
#define output_ctx_init unrealasm_exo_output_ctx_init
#define output_gamma_code unrealasm_exo_output_gamma_code
#define output_get_pos unrealasm_exo_output_get_pos
#define output_word unrealasm_exo_output_word
#define print_base_flags unrealasm_exo_print_base_flags
#define print_crunch_flags unrealasm_exo_print_crunch_flags
#define print_license unrealasm_exo_print_license
#define radix_node_get unrealasm_exo_radix_node_get
#define radix_node_set unrealasm_exo_radix_node_set
#define radix_tree_free unrealasm_exo_radix_tree_free
#define radix_tree_init unrealasm_exo_radix_tree_init
#define raw_log_formatter unrealasm_exo_raw_log_formatter
#define read_file unrealasm_exo_read_file
#define reverse_buffer unrealasm_exo_reverse_buffer
#define search_buffer unrealasm_exo_search_buffer
#define search_node_free unrealasm_exo_search_node_free
#define str_to_int unrealasm_exo_str_to_int
#define table_dump unrealasm_exo_table_dump
#define write_file unrealasm_exo_write_file

#include "exomizer.h"

/*
 * Exomizer_crunch without its command-line scaffolding (it allocates an argv it never frees and parses an empty
 * command line): the same default options, the same forward crunch (exocrunch), no reverse step.
 */
unsigned char *UnrealAsmExomizerCrunch(const unsigned char *input, int inputLength, int *outputLength)
{
    struct crunch_options options[1] = { CRUNCH_OPTIONS_DEFAULT };
    struct membuf inbuf[1];
    struct membuf outbuf[1];
    struct crunch_info info[1];
    unsigned char *output;
    int length;

    membuf_init(inbuf);
    membuf_init(outbuf);
    membuf_append(inbuf, input, inputLength);
    exocrunch(inbuf, outbuf, options, info);

    length = membuf_memlen(outbuf);
    output = (unsigned char *)malloc(length > 0 ? (size_t)length : 1u);
    if (output != NULL && length > 0)
    {
        memcpy(output, membuf_get(outbuf), (size_t)length);
    }
    *outputLength = output != NULL ? length : 0;

    membuf_free(outbuf);
    membuf_free(inbuf);
    return output;
}

void UnrealAsmExomizerFree(unsigned char *data)
{
    free(data);
}
