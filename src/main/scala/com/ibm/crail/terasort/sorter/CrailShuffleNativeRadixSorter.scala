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

import com.ibm.radixsort.NativeRadixSort
import org.apache.spark.TaskContext
import org.apache.spark.serializer.Serializer
import org.apache.spark.shuffle.crail.{CrailDeserializationStream, CrailShuffleSorter}
import sun.nio.ch.DirectBuffer

class CrailShuffleNativeRadixSorter extends CrailShuffleSorter {
  override def sort[K, C](context: TaskContext, keyOrd: Ordering[K], ser: Serializer, inputStream: CrailDeserializationStream): Iterator[Product2[K, C]] = {
    val buffer = inputStream.getFlatBuffer()
    NativeRadixSort.sort(buffer.asInstanceOf[DirectBuffer].address(), inputStream.numElements(),
      inputStream.keySize(), inputStream.keySize() + inputStream.valueSize())
    new ByteBufferIterator(inputStream).asInstanceOf[Iterator[Product2[K, C]]]
  }
}

private class ByteBufferIterator(inputStream: CrailDeserializationStream) extends Iterator[Product2[Array[Byte], Array[Byte]]] {

  private val key = new Array[Byte](inputStream.keySize())
  private val value = new Array[Byte](inputStream.valueSize())
  private val buffer = inputStream.getFlatBuffer()
  private var processed = 0
  private val numElements = inputStream.numElements()
  private val kv = (key, value)

  override def hasNext: Boolean = {
    processed < numElements
  }

  override def next(): Product2[Array[Byte], Array[Byte]] = {
    processed += 1
    buffer.get(key)
    buffer.get(value)
    kv
  }
}