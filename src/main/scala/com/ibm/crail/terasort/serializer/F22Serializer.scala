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

package com.ibm.crail.terasort.serializer

import java.io._
import java.nio.ByteBuffer

import com.ibm.crail.terasort.{TeraInputFormat, TeraSort}
import org.apache.crail.{CrailBufferedInputStream, CrailBufferedOutputStream}
import org.apache.spark.TaskContext
import org.apache.spark.serializer._

import scala.collection.mutable
import scala.collection.mutable.HashMap
import scala.reflect.ClassTag

class F22Serializer() extends CrailSparkSerializer {
  override final def newCrailSerializer(defaultSerializer: Serializer): CrailSerializerInstance = {
    F22ShuffleSerializerInstance.getInstance(defaultSerializer)
  }
}

class F22ShuffleSerializerInstance(defaultSerializer: Serializer) extends CrailSparkSerializerInstance(defaultSerializer) {

  override def serializeCrailStream(output: CrailBufferedOutputStream): CrailSerializationStream = {
    new F22SerializerStream(defaultSerializer, output)
  }

  override def deserializeCrailStream(input: CrailBufferedInputStream): CrailDeserializationStream = {
    new F22DeserializerStream(defaultSerializer, input)
  }
}

/* this implementation creates a new instance of the serializer for each new passed instance */
object F22ShuffleSerializerInstanceHashMap {
  private var serInstances:HashMap[Serializer, F22ShuffleSerializerInstance] =
    new mutable.HashMap[Serializer, F22ShuffleSerializerInstance]()
  final def getInstance(defaultSerializer: Serializer):F22ShuffleSerializerInstance = {
    this.synchronized {
      serInstances.get(defaultSerializer) match {
        case Some(cached:F22ShuffleSerializerInstance) => cached
        case None =>
          val newSer = new F22ShuffleSerializerInstance(defaultSerializer)
          serInstances(defaultSerializer) = newSer
          newSer
      }
    }
  }
}

/* this one keeps one singleton instance as we know internally how is it implemented */
object F22ShuffleSerializerInstance {
  private var serInstances:Option[F22ShuffleSerializerInstance] = None
  final def getInstance(defaultSerializer: Serializer):F22ShuffleSerializerInstance = {
    this.synchronized {
      serInstances match {
        case Some(cached:F22ShuffleSerializerInstance) => cached
        case None =>
          val newSer = new F22ShuffleSerializerInstance(defaultSerializer)
          serInstances = Some(newSer)
          newSer
      }
    }
  }
}

class F22SerializerStream(defaultSerializer: Serializer, outStream: CrailBufferedOutputStream)
  extends CrailSparkSerializationStream(defaultSerializer, outStream) {

  override final def writeObject[T: ClassTag](t: T): SerializationStream = {
    /* explicit byte casting */
    outStream.write(t.asInstanceOf[Array[Byte]])
    this
  }

  override final def flush(): Unit = {
    /* no op - spark io code explicitly purges the streams */
  }

  override final def writeKey[T: ClassTag](key: T): SerializationStream = writeObject(key)

  override final def writeValue[T: ClassTag](value: T): SerializationStream = writeObject(value)

  override final def close(): Unit = {
    if (outStream != null) {
      outStream.close()
    }
  }

  override final def writeAll[T: ClassTag](iter: Iterator[T]): SerializationStream = {
    while (iter.hasNext) {
      writeObject(iter.next())
    }
    this
  }
}

class F22DeserializerStream(defaultSerializer: Serializer, inStream: CrailBufferedInputStream)
  extends CrailSparkDeserializationStream(defaultSerializer, inStream) {

  override final def readObject[T: ClassTag](): T = {
    throw new IOException("this call is not yet supported : readObject " +
      " \n F22 is meant to use with readKey/readValue")
  }

  override def read(buf: ByteBuffer): Int = {
    val verbose = TaskContext.get().getLocalProperty(TeraSort.verboseKey).toBoolean
    val start = System.nanoTime()
    /* we attempt to read min(in file, buf.remaining) */
    val asked = buf.remaining()
    var soFar = 0
    while ( soFar < asked) {
      val ret = inStream.read(buf)
      if(ret == -1) {
        if(verbose) {
          val timeUs = (System.nanoTime() - start) / 1000
          val bw = soFar.asInstanceOf[Long] * 8 / (timeUs + 1) //just to avoid divide by zero error
          System.err.println(TeraSort.verbosePrefixF22 + " TID: " + TaskContext.get().taskAttemptId() +
            " crail reading bytes : " + soFar + " in " + timeUs + " usec or " + bw + " Mbps")
        }
        /* we have reached the end of the file */
        if(soFar == 0) {
          // if this was the first iteration then we immediately hit -1
          return -1
        }  else {
          // otherwise return what we have read so far
          return soFar
        }
      }
      soFar+=ret
    }
    // if we are here then we must have read the full data
    require(soFar == asked, " wrong read logic, asked: " + asked + " soFar " + soFar)
    if(verbose) {
      val timeUs = (System.nanoTime() - start) / 1000
      val bw = soFar.asInstanceOf[Long] * 8 / (timeUs + 1) //just to avoid divide by zero error
      System.err.println(TeraSort.verbosePrefixF22 + " TID: " + TaskContext.get().taskAttemptId() +
        " crail reading bytes : " + soFar + " in " + timeUs + " usec or " + bw + " Mbps")
    }
    soFar
  }

  override final def readKey[T: ClassTag](): T = {

    val key = new Array[Byte](TeraInputFormat.KEY_LEN)
    val ret = inStream.read(key)
    if(ret == -1) {
      /* mark the end of the stream : this is caught by spark to mark EOF - duh ! */
      throw new EOFException()
    }
    key.asInstanceOf[T]
  }

  override final def readValue[T: ClassTag](): T = {
    val value = new Array[Byte](TeraInputFormat.VALUE_LEN)
    val ret = inStream.read(value)
    if(ret == -1) {
      /* mark the end of the stream : this is caught by spark to mark EOF - duh ! */
      throw new EOFException()
    }
    value.asInstanceOf[T]
  }

  override final def close(): Unit = {
    if (inStream != null) {
      inStream.close()
    }
  }

  override def available(): Int = {
    inStream.available()
  }
}