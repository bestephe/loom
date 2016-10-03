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

import com.ibm.crail.terasort.{BufferCache, SerializerBuffer, TeraInputFormat}
import com.ibm.crail.{CrailBufferedOutputStream, CrailInputStream, CrailMultiStream}
import org.apache.spark.{ShuffleDependency, TaskContext}
import org.apache.spark.serializer.{DeserializationStream, SerializationStream, Serializer, SerializerInstance}
import org.apache.spark.shuffle.crail.{CrailDeserializationStream, CrailSerializationStream, CrailSerializerInstance, CrailShuffleSerializer}

import scala.reflect.ClassTag

class F22Serializer() extends Serializer with Serializable with CrailShuffleSerializer {
  override final def newInstance(): SerializerInstance = {
    F22ShuffleSerializerInstance.getInstance()
  }
  override lazy val supportsRelocationOfSerializedObjects: Boolean = true

  override def newCrailSerializer[K,V](dep: ShuffleDependency[K,_,V]): CrailSerializerInstance = {
    F22ShuffleSerializerInstance.getInstance()
  }
}

class F22ShuffleSerializerInstance() extends SerializerInstance with CrailSerializerInstance {

  override final def serialize[T: ClassTag](t: T): ByteBuffer = {
    throw new IOException("this call is not yet supported : serializer[] " +
    " \n perhaps you forgot to set spark.crail.shuffle.sorter setting in your spark conf to match F22")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer): T = {
    throw new IOException("this call is not yet supported : deserialize[]" +
      " \n perhaps you forgot to set spark.crail.shuffle.sorter setting in your spark conf to match F22")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer, loader: ClassLoader): T = {
    throw new IOException("this call is not yet supported : deserialize with classloader" +
      " \n perhaps you forgot to set spark.crail.shuffle.sorter setting in your spark conf to match F22")
  }

  override final def serializeStream(s: OutputStream): SerializationStream = {
    throw new IOException("this call is not yet supported : serializerStream with OutputStream " +
      " \n perhaps you forgot to set spark.crail.shuffle.sorter setting in your spark conf to match F22")
  }

  /* this is the one we are interested in */
  override final def deserializeStream(s: InputStream): DeserializationStream = {
    throw new IOException("this call is not yet supported : deserializerStream with InputStream " +
      " \n perhaps you forgot to set spark.crail.shuffle.sorter setting in your spark conf to match F22")
  }

  override def serializeCrailStream(s: CrailBufferedOutputStream): CrailSerializationStream = {
    new F22SerializerStream(s)
  }

  override def deserializeCrailStream(s: CrailMultiStream): CrailDeserializationStream = {
    new F22DeserializerStream(s)
  }
}

object F22ShuffleSerializerInstance {
  private var serIns:F22ShuffleSerializerInstance = null

  final def getInstance():F22ShuffleSerializerInstance = {
    this.synchronized {
      if(serIns == null)
        serIns = new F22ShuffleSerializerInstance()
    }
    serIns
  }
}

class F22SerializerStream(outStream: CrailBufferedOutputStream) extends CrailSerializationStream {

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

class F22DeserializerStream(inStream: CrailMultiStream) extends CrailDeserializationStream {
  val incomingData = inStream.available()
  /* FIXME: we don't need a unified buffer */
  val bufX :SerializerBuffer = BufferCache.getInstance().getUnifedBuffer(incomingData)

  var remaining = numElements()

  fillUpBuffer()

  override final def readObject[T: ClassTag](): T = {
    throw new IOException("this call is not yet supported : readObject " +
      " \n perhaps you forgot to set spark.crail.shuffle.sorter setting in your spark conf to match F22")
  }

  final def fillUpBuffer(): Unit = {
    val start = System.nanoTime()
    var so_far = 0
    val bb = bufX.getByteBuffer

    bb.limit(incomingData)
    bb.position(0)
    val ret = inStream.read(bb)
    if (ret < 0) {
      /* mark the end of the stream : this is spark's way off saying EOF */
      throw new EOFException()
    }
    /* we clean the ByteBuffer */
    bb.clear()
    val timeUs = (System.nanoTime() - start)/1000
    val bw = incomingData.asInstanceOf[Long] * 8/(timeUs + 1) //just to avoid divide by zero error
    System.err.println(" TS TID: " + TaskContext.get().taskAttemptId() +
      " crail reading bytes : " + incomingData + " in " + timeUs + " usec or " + bw + " Mbps")
  }

  override final def readKey[T: ClassTag](): T = {
    if(remaining == 0 ) {
      /* mark the end of the stream : this is caught by spark to mark EOF - duh ! */
      throw new EOFException()
    }
    val key = new Array[Byte](TeraInputFormat.KEY_LEN)
    bufX.getByteBuffer.get(key)
    remaining-=1
    key.asInstanceOf[T]
  }

  override final def readValue[T: ClassTag](): T = {
    if(remaining == 0 ) {
      /* mark the end of the stream : this is caught by spark to mark EOF - duh ! */
      throw new EOFException()
    }
    val value = new Array[Byte](TeraInputFormat.VALUE_LEN)
    bufX.getByteBuffer.get(value)
    value.asInstanceOf[T]
  }

  override final def close(): Unit = {
    if (inStream != null) {
      inStream.close()
    }
    if(bufX != null){
      BufferCache.getInstance().putBuffer(bufX)
    }
  }

  override def getFlatBuffer(): ByteBuffer = {
    bufX.getByteBuffer
  }

  override def valueSize(): Int = TeraInputFormat.VALUE_LEN

  override def keySize(): Int = TeraInputFormat.KEY_LEN

  override def numElements(): Int = incomingData /  TeraInputFormat.RECORD_LEN
}