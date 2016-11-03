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

package com.ibm.crail.terasort

import com.google.common.primitives.{Ints, Longs}
import org.apache.spark.Partitioner

/**
  * Partitioner for terasort. It uses the first seven bytes of the byte array to partition
  * the key space evenly as a Long.
  */
case class TeraSortPartitioner(numPartitions: Int) extends Partitioner{

  import TeraSortPartitioner._

  val rangePerPart = (max - min) / numPartitions

  override def getPartition(key: Any): Int = {
    val b = key.asInstanceOf[Array[Byte]]
    val prefix = Longs.fromBytes(0, b(0), b(1), b(2), b(3), b(4), b(5), b(6))
    (prefix / rangePerPart).toInt
  }
}

object TeraSortPartitioner {
  val min = Longs.fromBytes(0, 0, 0, 0, 0, 0, 0, 0)
  val max = Longs.fromBytes(0, -1, -1, -1, -1, -1, -1, -1)  // 0xff = -1
}

/**
  * Partitioner for terasort. It uses the first 3 bytes of the byte array to partition
  * the key space evenly as an Integer. This means that maximum number of partitions are
  * 2^24. Make sure that you don't run over that.
  */
case class TeraSortPartitionerInt(numPartitions: Int) extends Partitioner{

  import TeraSortPartitionerInt._

  val rem = diffInt % numPartitions
  val rangePerPartInt =
  if(rem == 0)
    diffInt/numPartitions
  else
    (diffInt + (numPartitions - rem))/numPartitions

  override def getPartition(key: Any): Int = {
    val b = key.asInstanceOf[Array[Byte]]
    val prefix = Ints.fromBytes(0, b(0), b(1), b(2))
    prefix / rangePerPartInt
  }
}

object TeraSortPartitionerInt {
  val minInt = Ints.fromBytes(0, 0, 0, 0)
  val maxInt = Ints.fromBytes(0, -1, -1, -1)  // 0xff = -1
  val diffInt = maxInt - minInt
}