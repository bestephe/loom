/*
 * Crail-terasort: An example terasort program for Sprak and crail
 *
 * Author: Jonas Pfefferle <jpf@zurich.ibm.com>
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

package com.ibm.crail.terasort.sorter

import scala.collection.mutable.ArrayBuffer

object MSDRadixSort {
  final def sort[V](records: ArrayBuffer[Product2[Array[Byte], V]], low: Int, high: Int, digit: Int): Unit = {
    if (low + 1 >= high || digit == -1) return

    val byte_mask = 1 << (digit % 8)
    val byte_idx = records(0)._1.length - (digit / 8) - 1
    var bin1_idx = high
    var bin0_idx = low
    while (bin0_idx < bin1_idx) {
      if ((records(bin0_idx)._1(byte_idx) & byte_mask) == byte_mask) {
        do {
          bin1_idx -= 1
        } while (bin1_idx > bin0_idx && (records(bin1_idx)._1(byte_idx) & byte_mask) == byte_mask)

        if (bin1_idx != bin0_idx) {
          val tmp = records(bin0_idx)
          records(bin0_idx) = records(bin1_idx)
          records(bin1_idx) = tmp
        } else {
          bin0_idx -= 1
        }
      }
      bin0_idx += 1
    }
    sort(records, low, bin0_idx, digit - 1)
    sort(records, bin0_idx, high, digit - 1)
  }
}
