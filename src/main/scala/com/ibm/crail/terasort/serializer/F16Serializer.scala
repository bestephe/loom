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

import com.ibm.crail.terasort.{TeraInputFormat, BufferCache}
import org.apache.spark.TaskContext
import org.apache.spark.serializer.{DeserializationStream, SerializationStream, SerializerInstance, Serializer}

import scala.reflect.ClassTag

class F16Serializer() extends Serializer with Serializable {
  override final def newInstance(): SerializerInstance = {
    F16Instance.getInstance()
  }
  override lazy val supportsRelocationOfSerializedObjects: Boolean = true
}


class F16Instance() extends SerializerInstance {

  override final def serialize[T: ClassTag](t: T): ByteBuffer = {
    throw new IOException("this call is not yet implemented : serializer[] ")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer): T = {
    throw new IOException("this call is not yet implemented : deserialize[]")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer, loader: ClassLoader): T = {
    throw new IOException("this call is not yet implemented : deserialize with classloader")
  }

  override final def serializeStream(s: OutputStream): SerializationStream = {
    new F16SerializerStream(s)
  }

  override final def deserializeStream(s: InputStream): DeserializationStream = {
    new F16DeserializerStream(s)
  }
}

object F16Instance {
  var serIns:F16Instance = null
  final def getInstance():F16Instance = {
    if(serIns == null){
      serIns = new F16Instance()
    }
    serIns
  }
}

class F16SerializerStream(outStream: OutputStream) extends SerializationStream {

  override final def writeObject[T: ClassTag](t: T): SerializationStream = {
    /* explicit byte casting */
    val x = t.asInstanceOf[Array[Byte]]
    outStream.write(x, 0, x.length)
    this
  }

  override final def flush() {
    if (outStream == null) {
      throw new IOException("Stream is closed")
    }
    outStream.flush()
  }

  override final def writeKey[T: ClassTag](key: T): SerializationStream = writeObject(key)
  /** Writes the object representing the value of a key-value pair. */
  override final def writeValue[T: ClassTag](value: T): SerializationStream = writeObject(value)
  override final def close(): Unit = {
    if (outStream != null) {
      try {
        outStream.close()
      } finally {
      }
    }
  }

  override final def writeAll[T: ClassTag](iter: Iterator[T]): SerializationStream = {
    while (iter.hasNext) {
      writeObject(iter.next())
    }
    this
  }
}

class F16DeserializerStream(inStream: InputStream) extends DeserializationStream {
  val incomingData = inStream.available()
  /* we need unified buffer */
  val unifiedBuf = BufferCache.getInstance().getByteArrayBuffer(incomingData)
  val buf = unifiedBuf.getByteArray
  fillUpBuffer()
  var index = 0

  override final def readObject[T: ClassTag](): T = {
    /* How do you read */
    throw new IOException("This call is not yet supported : readObject ")
  }

  final def fillUpBuffer(): Unit = {
    val start = System.nanoTime()
    var so_far = 0
    while (so_far < incomingData) {
      /* here we read into byte[] */
      val ret = inStream.read(buf, so_far, incomingData - so_far)
      if (ret < 0) {
        /* mark the end of the stream : this is spark's way off saying EOF */
        throw new EOFException()
      }
      so_far+=ret
    }
    val timeUs = (System.nanoTime() - start)/1000
    //just to avoid divide by zero error when run time is less than 1 usec for small partition sizes
    val bw = incomingData.asInstanceOf[Long] * 8/(timeUs + 1)
    System.err.println(" TS TID: " + TaskContext.get().taskAttemptId() +
      " crail reading bytes : " + incomingData + " in " + timeUs + " usec or " + bw + " Mbps")
  }

  override final def readKey[T: ClassTag](): T = {
    /* all capacities are the same and multiple of KV * 100 */
    if(index >= incomingData) {
      /* mark the end of the stream : this is caught by spark to mark EOF - duh ! */
      throw new EOFException()
    }
    val key = new Array[Byte](TeraInputFormat.KEY_LEN)
    System.arraycopy(buf, index, key, 0, TeraInputFormat.KEY_LEN)
    index+=TeraInputFormat.KEY_LEN
    key.asInstanceOf[T]
  }

  override final def readValue[T: ClassTag](): T = {
    if(index >= incomingData) {
      /* mark the end of the stream : this is caught by spark to mark EOF - duh ! */
      throw new EOFException()
    }
    val value = new Array[Byte](TeraInputFormat.VALUE_LEN)
    System.arraycopy(buf, index, value, 0, TeraInputFormat.VALUE_LEN)
    index+=TeraInputFormat.VALUE_LEN
    value.asInstanceOf[T]
  }

  override final def close(): Unit = {
    if (inStream != null) {
      try {
        inStream.close()
        BufferCache.getInstance().putBuffer(unifiedBuf)
      } finally {
      }
    }
  }
}