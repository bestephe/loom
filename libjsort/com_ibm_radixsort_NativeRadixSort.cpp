/*
 * Crail-terasort: An example terasort program for Sprak and crail
 *
 * Author: Animesh Trivedi <atr@zurich.ibm.com>
 *         Jonas Pfefferle <jpf@zurich.ibm.com>
 *
 * Copyright (C) 2016, IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "com_ibm_radixsort_NativeRadixSort.h"

#include <cstdint>
#include <cstring>
#include <cstddef>

#include <iterator>
#include <algorithm>

using namespace std;
#include <boost/sort/spreadsort/string_sort.hpp>

struct KV {
    union {
        uint8_t key[10];
        struct {
            uint64_t key1;
            uint16_t key2;
        } __attribute__((packed));
    };
    uint8_t value[90];
} __attribute__((packed));

struct lessthan {
    inline bool operator()(const KV& x, const KV& y) const {
        auto x_key1_be = __builtin_bswap64(x.key1);
        auto y_key1_be = __builtin_bswap64(y.key1);
        if (x_key1_be < y_key1_be) {
            return true;
        } else if (x_key1_be > y_key1_be) {
            return false;
        }
        auto x_key2_be = __builtin_bswap16(x.key2);
        auto y_key2_be = __builtin_bswap16(y.key2);
        if (x_key2_be < y_key2_be) {
            return true;
        }
        return false;
    }
};

struct bracket {
    inline uint8_t operator()(const KV& x, size_t offset) const {
        return x.key[offset];
    }
};

struct getsize {
    inline size_t operator()(const KV& x) const {
        return sizeof(x.key);
    }
};

/*
 * Class:     NativeRadixSort
 * Method:    sort
 * Signature: (JJJJ)V
 */
JNIEXPORT void JNICALL Java_com_ibm_radixsort_NativeRadixSort_sort
  (JNIEnv *env, jclass obj, jlong address, jint num, jlong key_length, jlong gap) {
    if (key_length != sizeof(KV::key) || gap != sizeof(KV)) {
        exit(-1);
    }
    KV* data = reinterpret_cast<KV*>(address);
    boost::sort::spreadsort::string_sort(data, data + num, bracket(), getsize(), lessthan());
}

