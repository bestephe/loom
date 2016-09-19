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
import javax.annotation.Nullable

import com.esotericsoftware.kryo.io.{Input => KryoInput, Output => KryoOuput}
import com.esotericsoftware.kryo.{Kryo, KryoException}
import com.ibm.crail.terasort.ParseTeraOptions
import org.apache.spark.serializer.{DeserializationStream, SerializationStream, Serializer, SerializerInstance}

import scala.reflect.ClassTag

class KryoSerializer(val parseTeraOptions: ParseTeraOptions) extends Serializer with Serializable {
  /* FIXME: can you have to cache thread instances here? */
  override final def newInstance(): SerializerInstance = {
    new KryoSerializerInstance(parseTeraOptions)
  }

  // This was the culprit that stops us from running serialized sort
  // https://github.com/apache/spark/pull/6415
  // https://issues.apache.org/jira/browse/SPARK-7873
  override lazy val supportsRelocationOfSerializedObjects: Boolean = {
    newInstance().asInstanceOf[KryoSerializerInstance].getAutoReset()
  }
}


class KryoSerializerInstance(val parseTeraOptions: ParseTeraOptions) extends SerializerInstance {
  /* before the borrow call */
  @Nullable private[this] var cachedKryo: Kryo = borrowKryo()

  def borrowKryo(): Kryo = {
    if (cachedKryo != null) {
      val kryo = cachedKryo
      kryo.reset()
      cachedKryo = null
      kryo
    } else {
      val kryo = new Kryo()
      /* this is the constructor */
      kryo.setRegistrationRequired(true)
      kryo.setReferences(false)
      /* we are not expecting ANY other type of class to pass from it */
      kryo.register(classOf[Array[Byte]], 1)
      kryo
    }
  }

  final def releaseKryo(kryo: Kryo): Unit = {
    if (cachedKryo == null) {
      cachedKryo = kryo
    }
  }

  /* now create input and output streams */
  //val input:KryoInput = new KryoInput(1048576)
  //val output:KryoOuput = new KryoOuput(1048576)

  override final def serialize[T: ClassTag](t: T): ByteBuffer = {
    /*
    val kryo = borrowKryo()
    output.clear()
    try {
      System.err.println("[SER] serialize called in TeraSerializerInstance")
      kryo.writeClassAndObject(output, t)
    } catch {
      case e: KryoException if e.getMessage.startsWith("Buffer overflow") =>
        throw new SparkException(s"Kryo serialization failed: ${e.getMessage}. To avoid this, " +
          "increase spark.kryoserializer.buffer.max value.")
    }finally {
      releaseKryo(kryo)
    }
    ByteBuffer.wrap(output.toBytes)
    */
    throw new IOException("this call is not yet supported ")
  }


  override final def deserialize[T: ClassTag](bytes: ByteBuffer): T = {
    /*
    val kryo = borrowKryo()
    try {
      input.setBuffer(bytes.array(), bytes.arrayOffset() + bytes.position(), bytes.remaining())
      kryo.readClassAndObject(input).asInstanceOf[T]
    }finally {
      releaseKryo(kryo)
    }
    */
    throw new IOException("this is not suppose to be called")
  }

  override final def deserialize[T: ClassTag](bytes: ByteBuffer, loader: ClassLoader): T = {
    /*
    val kryo = borrowKryo()
    val oldClassLoader = kryo.getClassLoader
    try {
      kryo.setClassLoader(loader)
      input.setBuffer(bytes.array(), bytes.arrayOffset() + bytes.position(), bytes.remaining())
      kryo.readClassAndObject(input).asInstanceOf[T]
    } finally {
      kryo.setClassLoader(oldClassLoader)
      releaseKryo(kryo)
    }
    */
    throw new IOException("this is not suppose to be called")
  }

  override final def serializeStream(s: OutputStream): SerializationStream = {
    new KryoSerializerStream(this, s)
  }

  override final def deserializeStream(s: InputStream): DeserializationStream = {
    new KryoDeserializerStream(this, s)
  }

  def getAutoReset(): Boolean = {
    val field = classOf[Kryo].getDeclaredField("autoReset")
    field.setAccessible(true)
    val kryo = borrowKryo()
    try {
      field.get(kryo).asInstanceOf[Boolean]
    } finally {
      releaseKryo(kryo)
    }
  }

}

class KryoSerializerStream(teraSerializerInstance: KryoSerializerInstance, outStream: OutputStream) extends SerializationStream {

  private[this] var output:KryoOuput = new KryoOuput(outStream,
    teraSerializerInstance.parseTeraOptions.getBufferSize)
  private[this] var kryo: Kryo = teraSerializerInstance.borrowKryo()

  override final def writeObject[T: ClassTag](t: T): SerializationStream = {
    //FIXME: this is a byte array - you can just write an object 
    kryo.writeClassAndObject(output, t)
    this
  }

  override final def flush() {
    if (output == null) {
      throw new IOException("Stream is closed")
    }
    output.flush()
  }

  override final def writeKey[T: ClassTag](key: T): SerializationStream = writeObject(key)
  /** Writes the object representing the value of a key-value pair. */
  override final def writeValue[T: ClassTag](value: T): SerializationStream = writeObject(value)
  override final def close(): Unit = {
    if (output != null) {
      try {
        output.close()
      } finally {
        teraSerializerInstance.releaseKryo(kryo)
        kryo = null
        output = null
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

class KryoDeserializerStream(teraSerializerInstance: KryoSerializerInstance, inStream: InputStream) extends DeserializationStream {

  private[this] var input: KryoInput = new KryoInput(inStream,
    teraSerializerInstance.parseTeraOptions.getBufferSize)
  private[this] var kryo: Kryo = teraSerializerInstance.borrowKryo()

  override final def readObject[T: ClassTag](): T = {
    try {
      //FIXME: this is a byte array
      kryo.readClassAndObject(input).asInstanceOf[T]
    } catch {
      case e: KryoException if e.getMessage.toLowerCase.contains("buffer underflow") =>
        throw new EOFException
    }
  }

  override final def readKey[T: ClassTag](): T = readObject[T]()
  override final def readValue[T: ClassTag](): T = readObject[T]()

  override final def close(): Unit = {
    if (input != null) {
      try {
        input.close()
      } finally {
        teraSerializerInstance.releaseKryo(kryo)
        kryo = null
        input = null
      }
    }
  }
  //override def asIterator: Iterator[Any] = ???
  //override def asKeyValueIterator: Iterator[(Any, Any)] = ???
}
