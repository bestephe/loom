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
import java.io.IOException
import java.nio.ByteBuffer
import com.ibm.crail.terasort.TeraInputFormat
import org.apache.spark.serializer.{DeserializationStream, SerializationStream, SerializerInstance, Serializer}

import scala.reflect.ClassTag

class ByteSerializer() extends Serializer with Serializable {
  override final def newInstance(): SerializerInstance = {
    ByteSerializerInstance.getInstance()
  }
  override lazy val supportsRelocationOfSerializedObjects: Boolean = true
}

class ByteSerializerInstance() extends SerializerInstance {

  override final def serialize[T: ClassTag](t: T): ByteBuffer = {
    throw new IOException("this call is not yet implemented : serializer[] ")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer): T = {
    throw new IOException("this call is not yet implemented : deserialize[]")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer, loader: ClassLoader): T = {
    throw new IOException("this call is not yet implemented : deserialize with classloader")
  }

  override final def serializeStream(o: OutputStream): SerializationStream = {
    new ByteSerializerStream(this, o)
  }

  override final def deserializeStream(i: InputStream): DeserializationStream = {
    new ByteDeserializerStream(this, i)
  }
}

object ByteSerializerInstance {
  private var serIns:ByteSerializerInstance = null

  final def getInstance():ByteSerializerInstance= {
    this.synchronized {
      if(serIns == null)
        serIns = new ByteSerializerInstance()
    }
    serIns
  }
}

class ByteSerializerStream(explicitByteSerializerInstance: ByteSerializerInstance,
                           outStream: OutputStream) extends SerializationStream {


  override final def writeObject[T: ClassTag](t: T): SerializationStream = {
    /* explicit byte casting */
    val tmp = t.asInstanceOf[Array[Byte]]
    outStream.write(tmp, 0, tmp.length)
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

class ByteDeserializerStream(explicitByteSerializerInstance: ByteSerializerInstance,
                             inStream: InputStream) extends DeserializationStream {

  override final def readObject[T: ClassTag](): T = {
    throw new IOException("this call is not yet implemented + readObject")
  }

  final def readBytes(bytes: Array[Byte]): Unit = {
    val ret = inStream.read(bytes, 0, bytes.length)
    if( ret < 0 ) {
      /* mark the end of the stream : this is spark's way off saying EOF */
      throw new EOFException()
    }
  }

  override final def readKey[T: ClassTag](): T = {
    val key = new Array[Byte](TeraInputFormat.KEY_LEN)
    readBytes(key)
    key.asInstanceOf[T]
  }

  override final def readValue[T: ClassTag](): T = {
    val value = new Array[Byte](TeraInputFormat.VALUE_LEN)
    readBytes(value)
    value.asInstanceOf[T]
  }

  override final def close(): Unit = {
    if (inStream != null) {
      inStream.close()
    }
  }
}
