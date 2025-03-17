/*
Copyright (c) 2018-2019, tevador <tevador@gmail.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the copyright holder nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#define RANDOMX_HASH_SIZE 32
#define RANDOMX_DATASET_ITEM_SIZE 64

#ifndef RANDOMX_EXPORT
#define RANDOMX_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int randomx_flags;
#define RANDOMX_FLAG_DEFAULT 0
#define RANDOMX_FLAG_LARGE_PAGES 1
#define RANDOMX_FLAG_HARD_AES 2
#define RANDOMX_FLAG_FULL_MEM 4
#define RANDOMX_FLAG_JIT 8
#define RANDOMX_FLAG_SECURE 16
#define RANDOMX_FLAG_ARGON2_SSSE3 32
#define RANDOMX_FLAG_ARGON2_AVX2 64
#define RANDOMX_FLAG_ARGON2 96

struct randomx_dataset;
struct randomx_cache;
struct randomx_vm;

typedef struct randomx_dataset randomx_dataset;
typedef struct randomx_cache randomx_cache;
typedef struct randomx_vm randomx_vm;

#ifdef __cplusplus
RANDOMX_EXPORT randomx_cache* randomx_alloc_cache(randomx_flags flags);
RANDOMX_EXPORT void randomx_init_cache(randomx_cache* cache, const void* key, size_t keySize);
RANDOMX_EXPORT void randomx_release_cache(randomx_cache* cache);
RANDOMX_EXPORT randomx_dataset* randomx_alloc_dataset(randomx_flags flags);
RANDOMX_EXPORT void randomx_init_dataset(randomx_dataset* dataset, randomx_cache* cache, unsigned long startItem, unsigned long itemCount);
RANDOMX_EXPORT void randomx_release_dataset(randomx_dataset* dataset);
RANDOMX_EXPORT randomx_vm* randomx_create_vm(randomx_flags flags, randomx_cache* cache, randomx_dataset* dataset);
RANDOMX_EXPORT void randomx_vm_set_cache(randomx_vm* machine, randomx_cache* cache);
RANDOMX_EXPORT void randomx_vm_set_dataset(randomx_vm* machine, randomx_dataset* dataset);
RANDOMX_EXPORT void randomx_destroy_vm(randomx_vm* machine);
RANDOMX_EXPORT void randomx_calculate_hash(randomx_vm* machine, const void* input, size_t inputSize, void* output);
RANDOMX_EXPORT void randomx_calculate_hash_first(randomx_vm* machine, const void* input, size_t inputSize);
RANDOMX_EXPORT void randomx_calculate_hash_next(randomx_vm* machine, const void* nextInput, size_t nextInputSize, void* output);
RANDOMX_EXPORT void randomx_calculate_hash_last(randomx_vm* machine, void* output);
RANDOMX_EXPORT void randomx_calculate_commitment(const void* input, size_t inputSize, const void* hash_in, void* com_out);
RANDOMX_EXPORT randomx_flags randomx_get_flags(void);
RANDOMX_EXPORT unsigned long randomx_dataset_item_count(void);
RANDOMX_EXPORT void* randomx_get_dataset_memory(randomx_dataset* dataset);
#else
RANDOMX_EXPORT struct randomx_cache* randomx_alloc_cache(randomx_flags flags);
RANDOMX_EXPORT void randomx_init_cache(struct randomx_cache* cache, const void* key, size_t keySize);
RANDOMX_EXPORT void randomx_release_cache(struct randomx_cache* cache);
RANDOMX_EXPORT struct randomx_dataset* randomx_alloc_dataset(randomx_flags flags);
RANDOMX_EXPORT void randomx_init_dataset(struct randomx_dataset* dataset, struct randomx_cache* cache, unsigned long startItem, unsigned long itemCount);
RANDOMX_EXPORT void randomx_release_dataset(struct randomx_dataset* dataset);
RANDOMX_EXPORT struct randomx_vm* randomx_create_vm(randomx_flags flags, struct randomx_cache* cache, struct randomx_dataset* dataset);
RANDOMX_EXPORT void randomx_vm_set_cache(struct randomx_vm* machine, struct randomx_cache* cache);
RANDOMX_EXPORT void randomx_vm_set_dataset(struct randomx_vm* machine, struct randomx_dataset* dataset);
RANDOMX_EXPORT void randomx_destroy_vm(struct randomx_vm* machine);
RANDOMX_EXPORT void randomx_calculate_hash(struct randomx_vm* machine, const void* input, size_t inputSize, void* output);
RANDOMX_EXPORT void randomx_calculate_hash_first(struct randomx_vm* machine, const void* input, size_t inputSize);
RANDOMX_EXPORT void randomx_calculate_hash_next(struct randomx_vm* machine, const void* nextInput, size_t nextInputSize, void* output);
RANDOMX_EXPORT void randomx_calculate_hash_last(struct randomx_vm* machine, void* output);
RANDOMX_EXPORT void randomx_calculate_commitment(const void* input, size_t inputSize, const void* hash_in, void* com_out);
RANDOMX_EXPORT randomx_flags randomx_get_flags(void);
RANDOMX_EXPORT unsigned long randomx_dataset_item_count(void);
RANDOMX_EXPORT void* randomx_get_dataset_memory(struct randomx_dataset* dataset);
#endif

#ifdef __cplusplus
}
#endif