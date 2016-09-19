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

import org.apache.spark.TaskContext
import org.apache.spark.serializer.Serializer
import org.apache.spark.shuffle.crail.{CrailDeserializationStream, CrailShuffleSorter}

import scala.collection.mutable.ArrayBuffer

class CrailShuffleRadixSorter extends CrailShuffleSorter {

  override def sort[K, C](context: TaskContext, keyOrd: Ordering[K], ser: Serializer, inputStream: CrailDeserializationStream): Iterator[Product2[K, C]] = {
    val buffer = new ArrayBuffer[Product2[Array[Byte], Array[Byte]]]
    var keySize = 0
    var valueSize = 0
    var firstIter = true
    val input = inputStream.asKeyValueIterator.asInstanceOf[Iterator[Product2[K, C]]]
    while (input.hasNext) {
      val kv = input.next().asInstanceOf[Product2[Array[Byte], Array[Byte]]]
      if (firstIter) {
        keySize = kv._1.length
        valueSize = kv._2.length
        firstIter = false
      } else if (kv._1.length != keySize || kv._2.length != valueSize) {
        /* we need the key and value size to be the same for all entries */
        throw new IllegalArgumentException("All key/value pairs have to be of same size")
      }
      buffer += kv
    }

    MSDRadixSort.sort(buffer, 0, buffer.size, buffer(0)._1.length * 8 - 1)
    buffer.iterator.asInstanceOf[Iterator[Product2[K, C]]]
  }
}